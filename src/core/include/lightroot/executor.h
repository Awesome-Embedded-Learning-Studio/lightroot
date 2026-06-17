/**
 * @file    executor.h
 * @author  Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief   Package build lifecycle executor.
 * @version 0.1
 * @date    2026-06-16
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Drives a resolved BuildPlan through the package lifecycle
 * (fetch -> verify -> extract -> configure -> build -> install-staging ->
 * install-target), writing per-phase logs and a JSONL event stream, and
 * populating staging/target rootfs directories. LightRoot owns the lifecycle
 * and the cross-compile environment; the package backend owns the per-phase
 * commands (declared in the manifest). Stage 3a implements the "script" backend
 * with "local" and "archive" (file://) sources.
 *
 * @ingroup lightroot_executor
 */
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <lightroot/config.h>
#include <lightroot/device_table.h>
#include <lightroot/manifest.h>
#include <lightroot/registry.h>
#include <lightroot/resolver.h>

namespace lightroot {

/**
 * @brief  Executor configuration.
 *
 * @since      0.1
 * @ingroup    lightroot_executor
 */
struct ExecutorConfig {
    ToolchainConfig toolchain; ///< Cross toolchain (drives the build env).
    std::filesystem::path output_dir =
        "out";    ///< Workspace root (downloads/build/staging/target/logs).
    int jobs = 0; ///< Build parallelism; 0 means auto (cpuCount()).

    /// Device table applied at assemble time. Empty => use the built-in default.
    DeviceTable device_table;

    /// Image formats to produce ("tar", "cpio", "ext4"). Default ["tar"].
    std::vector<std::string> image_formats{"tar"};
};

/**
 * @brief  Outcome of running a full plan.
 *
 * @since      0.1
 * @ingroup    lightroot_executor
 */
struct ExecutorResult {
    std::vector<std::string> built;   ///< Package names built successfully (in order).
    std::vector<std::string> failed;  ///< Package names that failed (first failure stops the run).
    std::filesystem::path events_log; ///< Path to the JSONL event stream.
    std::filesystem::path tar_path;   ///< Path to the assembled rootfs tar (empty if not produced).

    /// All rootfs image paths produced at assemble time (tar/cpio/ext4). The tar
    /// entry is also mirrored in @c tar_path for backward compatibility.
    std::vector<std::filesystem::path> image_paths;

    /**
     * @brief  Whether every step built (no failures).
     *
     * @return     true iff @c failed is empty.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_executor
     */
    [[nodiscard]] bool ok() const { return failed.empty(); }
};

/**
 * @brief  Default lifecycle phase commands for a well-known backend.
 *
 * @details Used when a manifest declares a `backend` but no explicit per-phase
 *          `run` commands. Known backends: "make-kconfig" (BusyBox-style),
 *          "make", "autotools". Returns an empty map for an unknown backend,
 *          in which case the package only goes through fetch/verify/extract.
 *
 * @param[in] backend Backend name from the manifest.
 *
 * @return     Phase key -> commands for the backend's canonical lifecycle.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_executor
 */
std::map<std::string, PhaseCommands> defaultPhases(std::string_view backend);

/**
 * @brief  Execute every step of a resolved plan through the lifecycle.
 *
 * @details Steps run in dependency order; the first failing phase stops the
 *          run (fail-fast). Each phase writes
 *          `out/logs/packages/<pkg>/<phase>.log` and appends an event to
 *          `out/logs/events.jsonl`.
 *
 * @param[in] reg  Registry backing the plan (used to read source/build detail).
 * @param[in] plan Resolved, topologically-ordered build plan.
 * @param[in] cfg  Executor configuration (toolchain, output dir, jobs).
 *
 * @return     Result with built/failed package lists and the events log path.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_executor
 */
ExecutorResult executePlan(const PackageRegistry& reg, const BuildPlan& plan,
                           const ExecutorConfig& cfg);

} // namespace lightroot
