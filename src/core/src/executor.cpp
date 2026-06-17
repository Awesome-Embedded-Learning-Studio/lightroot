#include <lightroot/executor.h>

#include <lightroot/exec.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace lightroot {
namespace {

using Env = std::vector<std::pair<std::string, std::string>>;

/// Known build-phase keys in lifecycle order.
constexpr std::string_view PHASES[] = {"patch", "configure", "build", "install_staging",
                                       "install_target"};

/// Join `parts` with `sep`.
std::string join(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out += sep;
        out += parts[i];
    }
    return out;
}

/// Single-quote `s` for safe interpolation into a `sh -c` command.
std::string shellQuote(std::string_view s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
}

/// Filename component of a URL or path (text after the last '/').
std::string urlBasename(std::string_view url) {
    const auto pos = url.rfind('/');
    return pos == std::string_view::npos ? std::string(url) : std::string(url.substr(pos + 1));
}

/// First whitespace-delimited token of `s` (trimmed).
std::string firstToken(std::string s) {
    const auto sp = s.find_first_of(" \t\r\n");
    if (sp != std::string::npos)
        s.erase(sp);
    return s;
}

/// Map a target triple to a Linux `make ARCH=...` value.
std::string linuxArch(std::string_view target) {
    if (target.rfind("aarch64", 0) == 0)
        return "arm64";
    if (target.rfind("arm", 0) == 0)
        return "arm";
    if (target.rfind("x86_64", 0) == 0)
        return "x86_64";
    if (target.rfind("i686", 0) == 0 || target.rfind("i586", 0) == 0)
        return "x86";
    const auto d = target.find('-');
    return std::string(d == std::string_view::npos ? target : target.substr(0, d));
}

/// Build the cross-compile environment for a phase.
Env crossEnv(const ToolchainConfig& tc, int jobs, const std::filesystem::path& destdir,
             const std::filesystem::path& out_root,
             const std::map<std::string, std::string>& extra) {
    Env env{{"CC", tc.prefix + "gcc"},
            {"CXX", tc.prefix + "g++"},
            {"AR", tc.prefix + "ar"},
            {"STRIP", tc.prefix + "strip"},
            {"RANLIB", tc.prefix + "ranlib"},
            {"LD", tc.prefix + "ld"},
            {"LIGHTROOT_TARGET", tc.target},
            {"LIGHTROOT_JOBS", std::to_string(jobs)},
            {"LIGHTROOT_PREFIX", "/usr"},
            {"LIGHTROOT_SYSROOT", tc.sysroot},
            {"LIGHTROOT_CROSS_COMPILE", tc.prefix},
            {"LIGHTROOT_ARCH", linuxArch(tc.target)},
            {"LIGHTROOT_OUT", out_root.string()}};
    if (!tc.sysroot.empty()) {
        const auto sf = "--sysroot=" + tc.sysroot;
        env.emplace_back("CFLAGS", sf);
        env.emplace_back("CXXFLAGS", sf);
        env.emplace_back("LDFLAGS", sf);
    }
    if (!destdir.empty()) {
        env.emplace_back("LIGHTROOT_DESTDIR", destdir.string());
        env.emplace_back("DESTDIR", destdir.string()); // make `install` convention.
    }
    for (const auto& [k, v] : extra)
        env.emplace_back(k, v);
    return env;
}

/// Run `cmd` via `sh -c` in `cwd`, writing combined output to `log`, emitting an
/// event; returns true on exit 0.
bool runShellPhase(EventLog& el, const std::string& pkg, const std::string& phase,
                   const std::string& cmd, const std::filesystem::path& cwd,
                   const std::filesystem::path& log, const Env& env) {
    const auto t0 = nowMs();
    const auto res = runCommand({"/bin/sh", "-c", cmd}, cwd, log, env);
    const auto dur = nowMs() - t0;
    el.event(pkg, phase, cmd, res.ok() ? "ok" : "fail", dur, log.string(),
             res.ok() ? "" : ("exit " + std::to_string(res.exit_code)));
    return res.ok();
}

/// sha256 of a file (empty string if sha256sum unavailable).
std::string sha256Of(const std::filesystem::path& p) {
    std::string out;
    runCapture({"sha256sum", p.string()}, {}, out);
    return firstToken(std::move(out));
}

/// Copy a local source directory into `workdir`, emitting a fetch event.
bool copyLocalSource(EventLog& el, const std::string& pkg, const std::filesystem::path& src,
                     const std::filesystem::path& workdir, const std::filesystem::path& log) {
    const auto t0 = nowMs();
    std::error_code ec;
    std::filesystem::copy(src, workdir,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          ec);
    const auto dur = nowMs() - t0;
    const bool ok = !ec;
    el.event(pkg, "fetch", std::string{"copy "} + src.string(), ok ? "ok" : "fail", dur,
             log.string(), ok ? "" : ec.message());
    return ok;
}

/// DESTDIR for an install phase (empty for non-install phases).
std::filesystem::path destDirFor(std::string_view phase, const std::filesystem::path& staging,
                                 const std::filesystem::path& target) {
    if (phase == "install_staging")
        return staging;
    if (phase == "install_target")
        return target;
    return {};
}

/// Resolve a local source URL: absolute paths as-is, relative paths against the
/// manifest's directory so shipped source trees resolve portably.
std::filesystem::path resolveSourcePath(const Package& p, std::string_view url) {
    const std::filesystem::path raw(url);
    if (url.empty() || raw.is_absolute())
        return raw;
    std::error_code ec;
    return std::filesystem::weakly_canonical(p.origin.parent_path() / raw, ec);
}

/// Synthesize the `patch` phase command: apply each declared patch (relative to
/// the manifest dir) in order with `patch -p1 -i`. Run in the extracted workdir.
std::string patchCommand(const Package& p) {
    std::vector<std::string> parts;
    for (const auto& patch : p.source->patches)
        parts.push_back("patch -p1 -i " + shellQuote(resolveSourcePath(p, patch).string()));
    return join(parts, " && ");
}

/// A rootfs image format the executor can produce.
enum class ImageFormat { Tar, Cpio, Ext4 };

/// Parse an image-format name from config ("tar" | "cpio" | "ext4").
std::optional<ImageFormat> parseImageFormat(std::string_view s) {
    if (s == "tar")
        return ImageFormat::Tar;
    if (s == "cpio")
        return ImageFormat::Cpio;
    if (s == "ext4")
        return ImageFormat::Ext4;
    return std::nullopt;
}

/// Format a mode as a 4-digit octal string ("0666", "1777").
std::string octal(unsigned mode) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04o", mode);
    return buf;
}

/// Path of `e.path` (`/dev/null`) relative to the rootfs root (`dev/null`).
std::string underRoot(std::string_view path) {
    std::string s(path);
    if (!s.empty() && s[0] == '/')
        s.erase(0, 1);
    return s;
}

/// Generate the assemble shell script. Run under fakeroot when @p degraded is
/// false (chown/mknod then succeed and are recorded); degraded mode skips them.
/// All paths are absolute and shell-quoted, so target paths with spaces survive.
std::string buildAssembleScript(const std::filesystem::path& target,
                                const std::filesystem::path& root, const DeviceTable& table,
                                const std::set<ImageFormat>& formats, bool degraded) {
    std::string s;
    s += "#!/bin/sh\n";
    s += "# Generated by LightRoot; assembles the rootfs. Run via: fakeroot /bin/sh <this>\n";
    s += "set -e\n";

    // 1. Normalize ownership to root (only meaningful under fakeroot).
    // NOTE: `find -exec chown {} +` (not `chown -R`). GNU `chown -R` streams a
    // continuous burst of lchown IPC at the single-threaded `faked` daemon and
    // wedges it (observed ~80% on a 400-entry rootfs under fakeroot 1.37). The
    // `find ... {} +` form batches files into a few chown processes, letting
    // faked drain between bursts — empirically reliable (0/6 wedge vs 5/6).
    if (!degraded)
        s += "find " + shellQuote(target.string()) + " -exec chown 0:0 {} +\n";

    // 2. Device table: directories then device nodes.
    for (const auto& e : table.entries) {
        const auto full = target / underRoot(e.path);
        if (e.kind == DeviceKind::Dir) {
            s += "mkdir -p " + shellQuote(full.string()) + "\n";
            s += "chmod " + octal(e.mode) + " " + shellQuote(full.string()) + "\n";
            if (!degraded)
                s += "chown 0:0 " + shellQuote(full.string()) + "\n";
        } else if (!degraded) {
            const char type = e.kind == DeviceKind::Block ? 'b' : 'c';
            s += "mkdir -p " + shellQuote(full.parent_path().string()) + "\n";
            s += "mknod " + shellQuote(full.string()) + " " + type + " " + std::to_string(e.major) +
                 " " + std::to_string(e.minor) + "\n";
            s += "chmod " + octal(e.mode) + " " + shellQuote(full.string()) + "\n";
            s += "chown 0:0 " + shellQuote(full.string()) + "\n";
        }
    }

    // 3. Images.
    if (formats.count(ImageFormat::Tar)) {
        s += "tar --owner=0 --group=0 -czf " + shellQuote((root / "rootfs.tar.gz").string()) +
             " -C " + shellQuote(target.string()) + " .\n";
    }
    if (formats.count(ImageFormat::Cpio)) {
        // --owner=0:0 forces root ownership in the archive (belt-and-suspenders
        // alongside the fakeroot chown above; GNU cpio copy-out honours it).
        s += "( cd " + shellQuote(target.string()) +
             " && find . | LC_ALL=C sort | cpio -o -H newc --owner=0:0 2>/dev/null | gzip > " +
             shellQuote((root / "rootfs.cpio.gz").string()) + " )\n";
    }
    if (formats.count(ImageFormat::Ext4) && !degraded) {
        const auto img = (root / "rootfs.ext4").string();
        const auto status = (root / "logs" / "ext4.status").string();
        // Size from real disk usage, +10% +4MiB headroom; retry once at 2x on failure.
        s += "__lr_mkext4() {\n";
        s += "  __sz=$(du -s --block-size=1K \"$1\" | awk '{print $1+0}')\n";
        s += "  __sz=$(( __sz * 11 / 10 + 4096 ))\n";
        s += "  truncate -s \"${__sz}K\" \"$2\" || return 1\n";
        s += "  mkfs.ext4 -F -d \"$1\" \"$2\" >/dev/null 2>&1 || return 1\n";
        s += "}\n";
        s += "if __lr_mkext4 " + shellQuote(target.string()) + " " + shellQuote(img) + "; then\n";
        s += "  echo ok > " + shellQuote(status) + "\n";
        s += "else\n";
        s += "  __sz=$(du -s --block-size=1K " + shellQuote(target.string()) +
             " | awk '{print $1+0}')\n";
        s += "  __sz=$(( __sz * 2 + 4096 ))\n";
        s += "  if truncate -s \"${__sz}K\" " + shellQuote(img) + " && mkfs.ext4 -F -d " +
             shellQuote(target.string()) + " " + shellQuote(img) + " >/dev/null 2>&1; then\n";
        s += "    echo ok > " + shellQuote(status) + "\n";
        s += "  else echo fail > " + shellQuote(status) + "; fi\n";
        s += "fi\n";
    }
    return s;
}

/// Append `p` to `out` when it exists on disk.
void addIfExists(std::vector<std::filesystem::path>& out, const std::filesystem::path& p) {
    std::error_code ec;
    if (std::filesystem::exists(p, ec))
        out.push_back(p);
}

/// Assemble the rootfs (device table + ownership + images) and record outputs.
void assembleRootfs(EventLog& el, const ExecutorConfig& cfg, const std::filesystem::path& root,
                    const std::filesystem::path& target, ExecutorResult& result) {
    std::set<ImageFormat> formats;
    for (const auto& name : cfg.image_formats) {
        if (auto f = parseImageFormat(name)) {
            formats.insert(*f);
        } else {
            el.event("rootfs", "assemble", "", "warn", 0, "",
                     "unknown image format '" + name + "' (expected tar|cpio|ext4)");
        }
    }
    if (formats.empty())
        formats.insert(ImageFormat::Tar);

    const DeviceTable table =
        cfg.device_table.entries.empty() ? defaultDeviceTable() : cfg.device_table;

    const bool have_fakeroot = findOnPath("fakeroot").has_value();
    if (!have_fakeroot)
        el.event("rootfs", "assemble", "", "warn", 0, "",
                 "fakeroot missing - output NOT hardware-correct (no device nodes, "
                 "build-user ownership)");

    const auto script_path = root / "logs" / "assemble.sh";
    {
        std::ofstream of(script_path);
        of << buildAssembleScript(target, root, table, formats, !have_fakeroot);
    }

    // NOTE: the `--` separator is required. fakeroot is a shell wrapper that
    // parses its own options; without `--` it can misparse the command's flags
    // and wedge its IPC with the `faked` daemon. `fakeroot -- /bin/sh <script>`
    // is the documented, safe form.
    std::vector<std::string> argv =
        have_fakeroot ? std::vector<std::string>{"fakeroot", "--", "/bin/sh", script_path.string()}
                      : std::vector<std::string>{"/bin/sh", script_path.string()};

    const auto log_path = root / "logs" / "assemble.log";
    const auto t0 = nowMs();
    const auto res = runCommand(argv, root, log_path, {});
    const auto dur = nowMs() - t0;
    el.event("rootfs", "assemble", join(argv, " "), res.ok() ? "ok" : "fail", dur,
             log_path.string(), res.ok() ? "" : ("exit " + std::to_string(res.exit_code)));
    if (!res.ok())
        return;

    if (formats.count(ImageFormat::Tar)) {
        const auto p = root / "rootfs.tar.gz";
        addIfExists(result.image_paths, p);
        if (!result.image_paths.empty() && result.image_paths.back() == p)
            result.tar_path = p; // mirror for backward compatibility
    }
    if (formats.count(ImageFormat::Cpio))
        addIfExists(result.image_paths, root / "rootfs.cpio.gz");
    if (formats.count(ImageFormat::Ext4)) {
        std::string marker = readFile(root / "logs" / "ext4.status");
        while (!marker.empty() && (marker.back() == '\n' || marker.back() == '\r' ||
                                   marker.back() == ' ' || marker.back() == '\t'))
            marker.pop_back();
        const auto p = root / "rootfs.ext4";
        std::error_code ec;
        if (marker == "ok" && std::filesystem::exists(p, ec))
            result.image_paths.push_back(p);
        else
            el.event("rootfs", "assemble", "", "warn", 0, "",
                     "ext4 image could not be produced; see " + log_path.string());
    }
}

} // namespace

std::map<std::string, PhaseCommands> defaultPhases(std::string_view backend) {
    std::map<std::string, PhaseCommands> out;
    if (backend == "make-kconfig") {
        // Verified against BusyBox 1.36.1 + aarch64-linux-gnu (2026-06): defconfig
        // then a static link and disabling the `tc` applet (CBQ structs dropped
        // from newer kernel headers) yields a clean static build.
        PhaseCommands cfg;
        cfg.run = {
            "make ARCH=$LIGHTROOT_ARCH CROSS_COMPILE=$LIGHTROOT_CROSS_COMPILE defconfig",
            "sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config",
            "sed -i 's/^CONFIG_TC=y/# CONFIG_TC is not set/' .config",
            "sed -i 's/^CONFIG_FEATURE_TC_INGRESS=y/# CONFIG_FEATURE_TC_INGRESS is not set/' "
            ".config",
        };
        out["configure"] = cfg;
        PhaseCommands build;
        build.run = {
            "make ARCH=$LIGHTROOT_ARCH CROSS_COMPILE=$LIGHTROOT_CROSS_COMPILE -j$LIGHTROOT_JOBS"};
        out["build"] = build;
        PhaseCommands inst;
        inst.run = {"make ARCH=$LIGHTROOT_ARCH CROSS_COMPILE=$LIGHTROOT_CROSS_COMPILE "
                    "CONFIG_PREFIX=$LIGHTROOT_DESTDIR install"};
        out["install_target"] = inst;
    } else if (backend == "make" || backend == "autotools") {
        PhaseCommands cfg;
        cfg.run = {"./configure --host=$LIGHTROOT_TARGET --prefix=/usr"};
        out["configure"] = cfg;
        PhaseCommands build;
        build.run = {"make -j$LIGHTROOT_JOBS"};
        out["build"] = build;
        PhaseCommands inst;
        inst.run = {"make DESTDIR=$LIGHTROOT_DESTDIR install"};
        out["install_target"] = inst;
    }
    return out;
}

ExecutorResult executePlan(const PackageRegistry& reg, const BuildPlan& plan,
                           const ExecutorConfig& cfg) {
    ExecutorResult result;
    // Absolute so DESTDIR/paths resolve correctly regardless of each child's cwd.
    const auto root = std::filesystem::absolute(
        cfg.output_dir.empty() ? std::filesystem::path{"out"} : cfg.output_dir);
    const auto downloads = root / "downloads";
    const auto build_root = root / "build";
    const auto staging = root / "staging";
    const auto target = root / "target";
    const auto packages_logs = root / "logs" / "packages";

    std::error_code ec;
    for (const auto& d : {downloads, build_root, staging, target, packages_logs})
        std::filesystem::create_directories(d, ec);

    const auto events_path = root / "logs" / "events.jsonl";
    EventLog el(events_path);
    result.events_log = events_path;
    const int jobs = cfg.jobs > 0 ? cfg.jobs : cpuCount();

    for (const auto& step : plan.steps) {
        const std::string pkg = step.name;
        const auto* p = reg.find(step.scope, pkg);
        if (!p) {
            result.failed.push_back(pkg);
            el.event(pkg, "resolve", "", "fail", 0, "", "package not found in registry");
            break;
        }

        const auto workdir = build_root / pkg;
        const auto pkg_logs = packages_logs / pkg;
        std::filesystem::create_directories(pkg_logs, ec);
        bool ok = true;

        // fetch / verify / extract
        if (p->source) {
            const auto& src = *p->source;
            if (src.kind == "local") {
                ok = copyLocalSource(el, pkg, resolveSourcePath(*p, src.url).string(), workdir,
                                     pkg_logs / "fetch.log");
            } else if (!src.url.empty()) {
                const auto arpath = downloads / urlBasename(src.url);
                ok = runShellPhase(el, pkg, "fetch",
                                   "curl -fL " + shellQuote(src.url) + " -o " +
                                       shellQuote(arpath.string()),
                                   downloads, pkg_logs / "fetch.log", {});
                if (ok && !src.sha256.empty()) {
                    const auto got = sha256Of(arpath);
                    if (got.empty()) {
                        el.event(pkg, "verify", "", "fail", 0, "", "sha256sum produced no output");
                        ok = false;
                    } else if (got != src.sha256) {
                        el.event(pkg, "verify", "", "fail", 0, "", "sha256 mismatch: got " + got);
                        ok = false;
                    } else {
                        el.event(pkg, "verify", "", "ok", 0, "", "");
                    }
                }
                if (ok) {
                    std::filesystem::create_directories(workdir, ec);
                    ok = runShellPhase(el, pkg, "extract",
                                       "tar -xf " + shellQuote(arpath.string()) + " -C " +
                                           shellQuote(workdir.string()) + " --strip-components=1",
                                       workdir, pkg_logs / "extract.log", {});
                }
            } else {
                el.event(pkg, "fetch", "", "skip", 0, "", "unknown source kind: " + src.kind);
            }
        } else {
            std::filesystem::create_directories(workdir, ec);
            el.event(pkg, "fetch", "", "skip", 0, "", "no source declared");
        }

        // configure / build / install_staging / install_target
        if (ok && p->build) {
            const auto phases = p->build->phases.empty() && !p->build->backend.empty()
                                    ? defaultPhases(p->build->backend)
                                    : p->build->phases;
            for (auto phase : PHASES) {
                // Resolve this phase's command: an explicit `[build.<phase>].run`
                // wins; for `patch` only, fall back to synthesizing `patch -p1`
                // commands from `[source].patches`.
                std::string cmd;
                const auto it = phases.find(std::string(phase));
                if (it != phases.end() && !it->second.run.empty())
                    cmd = join(it->second.run, " && ");
                else if (std::string_view(phase) == "patch" && p->source &&
                         !p->source->patches.empty())
                    cmd = patchCommand(*p);
                if (cmd.empty())
                    continue;
                const auto env = crossEnv(cfg.toolchain, jobs, destDirFor(phase, staging, target),
                                          root, p->build->env);
                ok = runShellPhase(el, pkg, std::string(phase), cmd, workdir,
                                   pkg_logs / (std::string(phase) + ".log"), env);
                if (!ok)
                    break;
            }
        }

        if (ok)
            result.built.push_back(pkg);
        else {
            result.failed.push_back(pkg);
            break;
        }
    }

    // Assemble the rootfs: under fakeroot apply the device table, normalize
    // ownership to root, and produce the requested image formats.
    if (result.ok() && !plan.steps.empty())
        assembleRootfs(el, cfg, root, target, result);

    return result;
}

} // namespace lightroot
