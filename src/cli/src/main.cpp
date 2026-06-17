#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <lightroot/config.h>
#include <lightroot/dependency.h>
#include <lightroot/device_table.h>
#include <lightroot/diagnostic.h>
#include <lightroot/doctor.h>
#include <lightroot/executor.h>
#include <lightroot/lockfile.h>
#include <lightroot/manifest.h>
#include <lightroot/registry.h>
#include <lightroot/resolver.h>
#include <lightroot/types.h>

namespace fs = std::filesystem;
using lightroot::PackageRegistry;
using lightroot::to_string;

namespace {

/// Scan package-tree roots recursively for *.toml manifests and index them.
int loadRegistry(PackageRegistry& reg, const std::vector<std::string>& roots) {
    int count = 0;
    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::exists(root, ec))
            continue;
        for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != end(it);
             it.increment(ec)) {
            const auto& entry = *it;
            if (!entry.is_regular_file() || entry.path().extension() != ".toml")
                continue;
            std::vector<lightroot::ManifestError> errs;
            if (auto pkg = lightroot::parseManifest(entry.path(), errs)) {
                reg.add(std::move(*pkg));
                ++count;
            }
            for (const auto& e : errs) {
                fmt::print(stderr, "warn: {}:{}: {}\n", entry.path().string(), e.line, e.message);
            }
        }
    }
    return count;
}

int runList(const std::vector<std::string>& roots) {
    PackageRegistry reg;
    loadRegistry(reg, roots);

    for (const auto& d : reg.duplicates()) {
        fmt::print(stderr, "warning: duplicate {}:{} (first: {}, overridden by: {})\n",
                   to_string(d.scope), d.name, d.first.string(), d.second.string());
    }

    for (const auto& p : reg.packages()) {
        fmt::print("{:<7} {:<14} v{:<10} {}\n", to_string(p.scope), p.name, p.version.to_string(),
                   p.summary.empty() ? std::string{"(no summary)"} : p.summary);
    }
    fmt::print(stderr, "\n{} package(s) indexed", reg.size());
    if (!reg.duplicates().empty()) {
        fmt::print(stderr, " ({} overridden)", reg.duplicates().size());
    }
    fmt::print(stderr, ".\n");
    return 0;
}

/// Normalize a provider map key by stripping a leading "virtual:" prefix, so
/// both "virtual:ssl" and "ssl" in [providers] map to capability "ssl".
std::string stripVirtual(std::string_view key) {
    constexpr std::string_view VP = "virtual:";
    if (key.size() >= VP.size() && key.substr(0, VP.size()) == VP) {
        return std::string(key.substr(VP.size()));
    }
    return std::string(key);
}

/// Loaded registry + resolved plan + toolchain, shared by resolve/build.
struct LoadedPlan {
    PackageRegistry reg;
    lightroot::BuildPlan plan;
    lightroot::ToolchainConfig toolchain;
    std::vector<std::string> image_formats{"tar"}; ///< From [rootfs].image_formats.
    std::filesystem::path device_table_file;       ///< From [rootfs].device_table.
    std::filesystem::path output_dir{"out"};       ///< From top-level output_dir.
    std::vector<std::string> selected;             ///< Root selections (lockfile context).
    std::map<std::string, std::string> providers;  ///< Provider bindings (lockfile context).
};

/// Scan package roots, parse the project config, and resolve the plan.
LoadedPlan loadAndResolve(const std::vector<std::string>& roots, const std::string& config_path) {
    LoadedPlan lp;
    loadRegistry(lp.reg, roots);

    std::map<std::string, std::string> explicit_providers;
    std::vector<lightroot::DependencyRef> selections;

    std::vector<lightroot::ConfigError> cerrs;
    if (auto cfg = lightroot::parseConfig(config_path, cerrs)) {
        lp.toolchain = cfg->toolchain;
        lp.image_formats = cfg->image_formats;
        lp.device_table_file = cfg->device_table_file;
        lp.output_dir = cfg->output_dir;
        lp.selected = cfg->selected;
        lp.providers = cfg->providers;
        for (const auto& [k, v] : cfg->providers)
            explicit_providers[stripVirtual(k)] = v;
        for (const auto& s : cfg->selected) {
            auto dep = lightroot::parseDependency(s);
            if (dep) {
                selections.push_back(*dep);
            } else {
                fmt::print(stderr, "warn: invalid selection '{}': {}\n", s, dep.error().message);
            }
        }
    } else {
        for (const auto& e : cerrs)
            fmt::print(stderr, "warn: config {}:{}: {}\n", config_path, e.line, e.message);
    }

    lightroot::Resolver resolver(lp.reg, std::move(explicit_providers));
    lp.plan = resolver.resolve(selections);
    return lp;
}

int runResolve(const std::vector<std::string>& roots, const std::string& config_path) {
    const auto lp = loadAndResolve(roots, config_path);
    std::cout << lightroot::to_json(lp.plan) << "\n";
    for (const auto& d : lp.plan.diagnostics)
        fmt::print(stderr, "{} [{}]: {}\n", to_string(d.severity), d.code, d.message);
    return lp.plan.ok() ? 0 : 1;
}

int runDoctor(const std::string& config_path) {
    std::vector<lightroot::ConfigError> cerrs;
    lightroot::ToolchainConfig tc;
    if (auto cfg = lightroot::parseConfig(config_path, cerrs)) {
        tc = cfg->toolchain;
    } else {
        for (const auto& e : cerrs) {
            fmt::print(stderr, "warn: config {}:{}: {}\n", config_path, e.line, e.message);
        }
        fmt::print(stderr, "warn: running doctor against an empty toolchain config\n");
    }

    const lightroot::DoctorReport rep = lightroot::runDoctor(tc);

    int pass = 0, warn = 0, fail = 0;
    for (const auto& c : rep.checks) {
        fmt::print("{:<5} {:<14} {}\n", lightroot::to_string(c.status), c.name, c.detail);
        if (c.status == lightroot::CheckStatus::Pass)
            ++pass;
        else if (c.status == lightroot::CheckStatus::Warn)
            ++warn;
        else
            ++fail;
    }
    fmt::print(stderr, "\n{} passed, {} warning(s), {} failed\n", pass, warn, fail);
    return rep.ok() ? 0 : 1;
}

int runBuild(const std::vector<std::string>& roots, const std::string& config_path,
             const std::vector<std::string>& image_format_overrides,
             const std::string& device_table_override, const std::string& lock_path,
             bool update_lock) {
    const auto lp = loadAndResolve(roots, config_path);

    if (!lp.plan.ok()) {
        fmt::print(stderr, "resolve: plan has errors; aborting build.\n");
        for (const auto& d : lp.plan.diagnostics)
            fmt::print(stderr, "  {} [{}]: {}\n", to_string(d.severity), d.code, d.message);
        return 1;
    }

    // Lockfile gate: pin reproducibility. `--update-lock` refreshes it; otherwise
    // a present lockfile must match the freshly-resolved plan or the build is
    // refused, so a dependency/provider/version change never rebuilds silently.
    if (update_lock) {
        if (!lightroot::writeLockfile(fs::path{lock_path}, lp.plan, lp.toolchain, lp.selected,
                                      lp.providers)) {
            fmt::print(stderr, "lock: failed to write {}\n", lock_path);
            return 1;
        }
        fmt::print(stderr, "lock: wrote {} ({} packages)\n", lock_path, lp.plan.steps.size());
    } else {
        std::error_code ec;
        if (fs::exists(lock_path, ec)) {
            std::vector<lightroot::LockfileError> lerrs;
            if (auto lock = lightroot::parseLockfile(lock_path, lerrs)) {
                const auto drift = lightroot::checkLockfileDrift(*lock, lp.plan);
                if (!drift.empty()) {
                    fmt::print(stderr, "lock: drift vs {} — build refused for reproducibility.\n",
                               lock_path);
                    fmt::print(stderr, "{}", drift.summary());
                    fmt::print(
                        stderr,
                        "refresh with `lightroot lock` or `lightroot build --update-lock`.\n");
                    return 1;
                }
            } else {
                // An unreadable lockfile warns but does not block (don't fail on corruption).
                for (const auto& e : lerrs)
                    fmt::print(stderr, "warn: lockfile: {}\n", e.message);
            }
        }
    }

    // Doctor gate: refuse to build on a failing toolchain.
    const auto report = lightroot::runDoctor(lp.toolchain);
    if (!report.ok()) {
        fmt::print(stderr, "doctor: toolchain/host checks failed; aborting build.\n");
        for (const auto& c : report.checks)
            fmt::print(stderr, "  {} {}\n", lightroot::to_string(c.status), c.name);
        return 1;
    }

    lightroot::ExecutorConfig ecfg;
    ecfg.toolchain = lp.toolchain;
    ecfg.output_dir = lp.output_dir;
    ecfg.image_formats = image_format_overrides.empty() ? lp.image_formats : image_format_overrides;

    const fs::path device_file =
        device_table_override.empty() ? lp.device_table_file : fs::path{device_table_override};
    std::vector<lightroot::DeviceTableError> derrs;
    ecfg.device_table =
        lightroot::resolveDeviceTable(device_file, fs::path{config_path}.parent_path(), derrs);
    for (const auto& e : derrs)
        fmt::print(stderr, "warn: device table: {}\n", e.message);

    const auto result = lightroot::executePlan(lp.reg, lp.plan, ecfg);

    for (const auto& name : result.built)
        fmt::print("BUILD  {}\n", name);
    for (const auto& name : result.failed)
        fmt::print(stderr, "FAIL   {}\n", name);
    fmt::print(stderr, "\n{} built, {} failed\n", result.built.size(), result.failed.size());
    fmt::print(stderr, "events: {}\n", result.events_log.string());
    if (!result.tar_path.empty())
        fmt::print(stderr, "rootfs: {}\n", result.tar_path.string());
    for (const auto& img : result.image_paths)
        fmt::print(stderr, "image:  {}\n", img.string());
    return result.ok() ? 0 : 1;
}

int runLock(const std::vector<std::string>& roots, const std::string& config_path,
            const std::string& lock_path) {
    const auto lp = loadAndResolve(roots, config_path);
    if (!lp.plan.ok()) {
        fmt::print(stderr, "resolve: plan has errors; cannot lock.\n");
        for (const auto& d : lp.plan.diagnostics)
            fmt::print(stderr, "  {} [{}]: {}\n", to_string(d.severity), d.code, d.message);
        return 1;
    }
    if (!lightroot::writeLockfile(fs::path{lock_path}, lp.plan, lp.toolchain, lp.selected,
                                  lp.providers)) {
        fmt::print(stderr, "lock: failed to write {}\n", lock_path);
        return 1;
    }
    fmt::print("lock: wrote {} ({} packages)\n", lock_path, lp.plan.steps.size());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    CLI::App app{"LightRoot — embedded rootfs builder"};
    app.require_subcommand(0, 1);

    std::vector<std::string> pkg_roots{"packages/upstream", "packages/company"};

    std::string config_path{"lightroot.toml"};

    std::vector<std::string> build_image_formats; // overrides [rootfs].image_formats
    std::string build_device_table;               // overrides [rootfs].device_table
    std::string lock_path{"lightroot.lock"};      // lockfile path (lock/build)
    bool build_update_lock = false;               // build: refresh the lockfile

    auto* list_cmd = app.add_subcommand("list", "List indexed packages");
    list_cmd->add_option("--root,-r", pkg_roots, "Package tree roots to scan")
        ->expected(1, -1)
        ->capture_default_str();

    auto* resolve_cmd =
        app.add_subcommand("resolve", "Resolve dependencies and providers into a build plan");
    resolve_cmd->add_option("--root,-r", pkg_roots, "Package tree roots to scan")
        ->expected(1, -1)
        ->capture_default_str();
    resolve_cmd->add_option("--config,-c", config_path, "Project config file")
        ->capture_default_str();

    auto* doctor_cmd = app.add_subcommand("doctor", "Check host and toolchain readiness");
    doctor_cmd->add_option("--config,-c", config_path, "Project config file")
        ->capture_default_str();

    auto* build_cmd = app.add_subcommand("build", "Build the resolved plan into a rootfs");
    build_cmd->add_option("--root,-r", pkg_roots, "Package tree roots to scan")
        ->expected(1, -1)
        ->capture_default_str();
    build_cmd->add_option("--config,-c", config_path, "Project config file")->capture_default_str();
    build_cmd
        ->add_option("--image-format,-f", build_image_formats,
                     "Image formats to produce (tar|cpio|ext4); overrides [rootfs].image_formats")
        ->expected(0, -1);
    build_cmd->add_option("--device-table", build_device_table,
                          "Device-table TOML file; overrides [rootfs].device_table");
    build_cmd->add_option("--lock", lock_path, "Lockfile path")->capture_default_str();
    build_cmd->add_flag("--update-lock", build_update_lock,
                        "Refresh the lockfile from the current plan, then build");

    auto* lock_cmd = app.add_subcommand("lock", "Write a reproducible-build lockfile");
    lock_cmd->add_option("--root,-r", pkg_roots, "Package tree roots to scan")
        ->expected(1, -1)
        ->capture_default_str();
    lock_cmd->add_option("--config,-c", config_path, "Project config file")->capture_default_str();
    lock_cmd->add_option("--lock", lock_path, "Lockfile path")->capture_default_str();

    CLI11_PARSE(app, argc, argv);

    const auto subs = app.get_subcommands();
    if (subs.empty()) {
        std::cout << app.help();
        return 0;
    }
    const std::string name = subs[0]->get_name();
    if (name == "list")
        return runList(pkg_roots);
    if (name == "resolve")
        return runResolve(pkg_roots, config_path);
    if (name == "doctor")
        return runDoctor(config_path);
    if (name == "build")
        return runBuild(pkg_roots, config_path, build_image_formats, build_device_table, lock_path,
                        build_update_lock);
    if (name == "lock")
        return runLock(pkg_roots, config_path, lock_path);
    std::cout << app.help();
    return 0;
}
