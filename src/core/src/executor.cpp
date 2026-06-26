#include <lightroot/executor.h>

#include <lightroot/backend.h>
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
enum class ImageFormat { Tar, Cpio, Ext4, Sdcard };

/// Parse an image-format name from config ("tar" | "cpio" | "ext4" | "sdcard").
std::optional<ImageFormat> parseImageFormat(std::string_view s) {
    if (s == "tar")
        return ImageFormat::Tar;
    if (s == "cpio")
        return ImageFormat::Cpio;
    if (s == "ext4")
        return ImageFormat::Ext4;
    if (s == "sdcard")
        return ImageFormat::Sdcard;
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

/// Render the shell block that builds a data-described partitioned image (the
/// [image] table). Board-agnostic: every offset, partition, filesystem and boot
/// file comes from @p img; this code knows nothing about any specific board. The
/// block writes a `partimg-<output>.status` marker so assembleRootfs can collect
/// the artifact. Requires sfdisk + mkfs.ext4 (no root); skipped under fakeroot
/// absence because the rootfs must be hardware-correct first.
std::string buildPartitionedImageScript(const std::filesystem::path& target,
                                         const std::filesystem::path& root,
                                         const PartitionedImage& img) {
    std::string s;
    const auto out = (root / img.output).string();
    const auto status = (root / "logs" / ("partimg-" + img.output + ".status")).string();
    s += "# Partitioned image rendered from the [image] table (layout is data).\n";
    s += "__lr_mkpartimg() {\n";
    s += "  __t=" + shellQuote(target.string()) + "\n";
    s += "  __o=" + shellQuote(out) + "\n";
    s += "  __a=" + shellQuote(root.string()) + "\n";
    // Per-partition size in MiB, then sectors. "auto" sizes a rootfs partition
    // from the assembled rootfs usage + headroom; anything else is a decimal MiB.
    for (std::size_t i = 0; i < img.partitions.size(); ++i) {
        const auto& p = img.partitions[i];
        const auto iv = std::to_string(i);
        if (p.rootfs && p.size == "auto") {
            s += "  __PSZ_" + iv + "=$( ( du -s --block-size=1M \"$__t\" | awk '{print $1+0}' ) )\n";
            s += "  __PSZ_" + iv + "=$(( __PSZ_" + iv + " * 3 / 2 + 64 ))\n";
            s += "  [ \"$__PSZ_" + iv + "\" -lt 64 ] && __PSZ_" + iv + "=64\n";
        } else {
            unsigned mib = 64;
            try {
                mib = static_cast<unsigned>(std::stoul(p.size));
            } catch (...) {
            }
            s += "  __PSZ_" + iv + "=" + std::to_string(mib) + "\n";
        }
        s += "  __PSEC_" + iv + "=$(( __PSZ_" + iv + " * 2048 ))\n";
    }
    // Image size = the furthest partition end + 8 MiB slack.
    s += "  __end=0\n";
    for (std::size_t i = 0; i < img.partitions.size(); ++i) {
        const auto iv = std::to_string(i);
        const unsigned start = img.partitions[i].start_mib;
        s += "  __end=$(( __PSZ_" + iv + " + " + std::to_string(start) + " > __end ? __PSZ_" + iv +
             " + " + std::to_string(start) + " : __end ))\n";
    }
    s += "  truncate -s \"$(( __end + 8 ))M\" \"$__o\" || return 1\n";
    // Optional pre-partition payload (e.g. bootloader at a fixed offset).
    if (img.payload && !img.payload->file.empty()) {
        s += "  dd if=\"$__a/" + img.payload->file + "\" of=\"$__o\" bs=1K seek=" +
             std::to_string(img.payload->offset_kib) + " conv=notrunc status=none || return 1\n";
    }
    // MBR partition table. Starts are compile-time constants; sizes are the
    // computed sector counts, expanded by the shell when building $__pt.
    s += "  __pt=\"label: dos\"\n";
    for (std::size_t i = 0; i < img.partitions.size(); ++i) {
        const auto& p = img.partitions[i];
        char typehex[8];
        std::snprintf(typehex, sizeof(typehex), "%x", p.type);
        s += "  __pt=\"$__pt\nstart=" + std::to_string(p.start_mib * 2048) + ",size=$__PSEC_" +
             std::to_string(i) + ",type=" + typehex + (p.bootable ? ",bootable" : "") + "\"\n";
    }
    s += "  printf '%s\\n' \"$__pt\" | sfdisk --quiet --no-reread \"$__o\" >/dev/null 2>&1 || return 1\n";
    // Per-partition filesystem, built from either the rootfs tree or declared
    // files, then dd'd into its slot.
    for (std::size_t i = 0; i < img.partitions.size(); ++i) {
        const auto& p = img.partitions[i];
        const auto iv = std::to_string(i);
        const std::string start_sec = std::to_string(p.start_mib * 2048);
        if (p.rootfs) {
            s += "  __cd_" + iv + "=\"$__t\"\n";
        } else {
            s += "  __cd_" + iv + "=$(mktemp -d)\n";
            for (std::size_t j = 0; j < p.files.size(); ++j) {
                const auto& f = p.files[j];
                const std::string dst = f.dst.empty() ? f.src : f.dst;
                if (dst.empty())
                    continue;
                if (!f.src.empty()) {
                    s += "  cp \"$__a/" + f.src + "\" \"$__cd_" + iv + "/" + dst + "\"\n";
                } else {
                    const auto delim = "__LRF" + iv + "_" + std::to_string(j) + "__";
                    s += "  cat > \"$__cd_" + iv + "/" + dst + "\" <<'" + delim + "'\n";
                    s += f.inline_text;
                    if (f.inline_text.empty() || f.inline_text.back() != '\n')
                        s += "\n";
                    s += delim + "\n";
                }
            }
        }
        const std::string label = p.label.empty() ? std::string{"root"} : p.label;
        s += "  truncate -s \"${__PSZ_" + iv + "}M\" \"$__o.p" + iv + "\" || return 1\n";
        s += "  mkfs.ext4 -F -q -d \"$__cd_" + iv + "\" -L " + label + " -m 0 \"$__o.p" + iv +
             "\" >/dev/null 2>&1 || return 1\n";
        s += "  dd if=\"$__o.p" + iv + "\" of=\"$__o\" bs=512 seek=" + start_sec +
             " conv=notrunc status=none || return 1\n";
        s += "  rm -f \"$__o.p" + iv + "\"\n";
        if (!p.rootfs)
            s += "  rm -rf \"$__cd_" + iv + "\"\n";
    }
    s += "  return 0\n";
    s += "}\n";
    s += "if __lr_mkpartimg; then echo ok > " + shellQuote(status) + "\n";
    s += "else echo fail > " + shellQuote(status) + "; fi\n";
    return s;
}

/// Generate the assemble shell script. Run under fakeroot when @p degraded is
/// false (chown/mknod then succeed and are recorded); degraded mode skips them.
/// All paths are absolute and shell-quoted, so target paths with spaces survive.
std::string buildAssembleScript(const std::filesystem::path& target,
                                const std::filesystem::path& root, const DeviceTable& table,
                                const std::set<ImageFormat>& formats, bool degraded,
                                const std::optional<PartitionedImage>& image) {
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

    // 3. If a kernel was built (out/Image), drop it at /boot/Image so every image
    //    format carries it — most importantly the sdcard image, which is meant to
    //    be a self-contained flashable disk (bootloader loads /boot/Image).
    {
        const auto kernel = root / "Image";
        s += "if [ -f " + shellQuote(kernel.string()) + " ]; then mkdir -p " +
             shellQuote((target / "boot").string()) + "; cp " + shellQuote(kernel.string()) + " " +
             shellQuote((target / "boot" / "Image").string()) + "; fi\n";
    }

    // 4. Images.
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
    if (formats.count(ImageFormat::Sdcard) && !degraded) {
        const auto img = (root / "sdcard.img").string();
        const auto status = (root / "logs" / "sdcard.status").string();
        // A flashable disk image: an MBR partition table + a single ext4
        // partition populated from the rootfs (with the kernel at /boot/Image).
        // 1 MiB gap before sector 2048 (partition alignment). mkfs.ext4 -E offset
        // formats the partition in place; sfdisk writes the table (no root needed).
        s += "__lr_mksdcard() {\n";
        s += "  __sz=$(du -s --block-size=1K \"$1\" | awk '{print $1+0}')\n";
        s += "  __sz=$(( __sz * 11 / 10 + 8192 ))\n";
        s += "  truncate -s $(( 2048 * 512 + __sz * 1024 )) \"$2\" || return 1\n";
        s += "  printf 'label: dos\\nstart=2048,type=83\\n' | sfdisk \"$2\" >/dev/null 2>&1 || "
             "return 1\n";
        s += "  mkfs.ext4 -E offset=$(( 2048 * 512 )) -F -q -d \"$1\" \"$2\" >/dev/null 2>&1 || "
             "return 1\n";
        s += "}\n";
        s += "if __lr_mksdcard " + shellQuote(target.string()) + " " + shellQuote(img) +
             "; then echo ok > " + shellQuote(status) + "\n";
        s += "else echo fail > " + shellQuote(status) + "; fi\n";
    }
    if (image && !degraded)
        s += buildPartitionedImageScript(target, root, *image);
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
        of << buildAssembleScript(target, root, table, formats, !have_fakeroot, cfg.image);
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
    if (formats.count(ImageFormat::Sdcard)) {
        std::string marker = readFile(root / "logs" / "sdcard.status");
        while (!marker.empty() && (marker.back() == '\n' || marker.back() == '\r' ||
                                   marker.back() == ' ' || marker.back() == '\t'))
            marker.pop_back();
        const auto p = root / "sdcard.img";
        std::error_code ec;
        if (marker == "ok" && std::filesystem::exists(p, ec))
            result.image_paths.push_back(p);
        else
            el.event("rootfs", "assemble", "", "warn", 0, "",
                     "sdcard image could not be produced; see " + log_path.string());
    }
    if (cfg.image) {
        std::string marker = readFile(root / "logs" / ("partimg-" + cfg.image->output + ".status"));
        while (!marker.empty() && (marker.back() == '\n' || marker.back() == '\r' ||
                                   marker.back() == ' ' || marker.back() == '\t'))
            marker.pop_back();
        const auto p = root / cfg.image->output;
        std::error_code ec;
        if (marker == "ok" && std::filesystem::exists(p, ec))
            result.image_paths.push_back(p);
        else
            el.event("rootfs", "assemble", "", "warn", 0, "",
                     "partitioned image '" + cfg.image->output +
                         "' could not be produced; see " + log_path.string());
    }
}

} // namespace

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
            // Start from a clean workdir. A previous run may have left a patched/
            // extracted tree behind (e.g. files a patch added), which would make a
            // re-run's extract+patch fail with "already exists". Wipe + recreate so
            // every build reproduces from pristine source.
            std::error_code clean_ec;
            std::filesystem::remove_all(workdir, clean_ec);
            std::filesystem::create_directories(workdir, clean_ec);
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

            // Backend setup: generate any toolchain/cross files into the workdir
            // and expose their paths via env vars (e.g. $LIGHTROOT_CMAKE_TOOLCHAIN),
            // merged onto the manifest's phase env. Generated regardless of whether
            // the manifest overrides the default phases, so custom commands can
            // still reference them.
            auto phase_env = p->build->env;
            // Expose staging so a package can consume already-built dependencies
            // (e.g. autotools `--with-zlib=$LIGHTROOT_STAGING`); backends that embed
            // it in a generated file (cmake/meson) also get it here for ad-hoc use.
            phase_env["LIGHTROOT_STAGING"] = staging.string();
            for (const auto& f : backendFiles(p->build->backend, cfg.toolchain, staging)) {
                const auto fpath = workdir / f.filename;
                std::ofstream of(fpath);
                if (!of) {
                    el.event(pkg, "backend", "", "fail", 0, "", "cannot write " + f.filename);
                    ok = false;
                    break;
                }
                of << f.content;
                phase_env[f.env_var] = fpath.string();
            }

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
                                          root, phase_env);
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
