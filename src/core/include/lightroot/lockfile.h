/**
 * @file    lockfile.h
 * @author  Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief   Reproducible-build lockfile: pin a resolved plan's package selection.
 * @version 0.1
 * @date    2026-06-17
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * A lockfile snapshots a resolved BuildPlan's package closure (name, scope,
 * version per step) plus the context that produced it (target triple, root
 * selections, explicit provider bindings). Committing it makes a build
 * reproducible: `lightroot build` re-resolves and compares against the lockfile,
 * failing loudly on any drift (a dependency added/removed/version-bumped, or a
 * provider changed) instead of silently rebuilding something different. This is
 * the Cargo.lock / package-lock.json semantic applied to a rootfs builder.
 *
 * The lockfile is JSON (machine-managed, committed to the repo), produced by
 * `lightroot lock` and consumed by the build drift check. Parsing is multi-error
 * (`optional<Lockfile>` + an out-parameter error vector), mirroring the other
 * parsers.
 *
 * @ingroup lightroot_lockfile
 */
#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <lightroot/config.h>
#include <lightroot/resolver.h>
#include <lightroot/types.h>
#include <lightroot/version.h>

namespace lightroot {

/**
 * @brief  One pinned package in a lockfile.
 *
 * @since      0.1
 * @ingroup    lightroot_lockfile
 */
struct LockedStep {
    Scope scope = Scope::Target; ///< Host or target scope.
    std::string name;            ///< Package name.
    Version version;             ///< Pinned version.
};

/**
 * @brief  A parsed lockfile: pinned closure + producing context.
 *
 * @since      0.1
 * @ingroup    lightroot_lockfile
 */
struct Lockfile {
    int schema_version = 1;                       ///< Lockfile format version.
    std::string target;                           ///< Toolchain target triple (context).
    std::vector<std::string> roots;               ///< Top-level selected roots (context).
    std::map<std::string, std::string> providers; ///< Capability -> package (context).
    std::vector<LockedStep> steps;                ///< Pinned package closure.
};

/**
 * @brief  An error raised while parsing a lockfile.
 *
 * @since      0.1
 * @ingroup    lightroot_lockfile
 */
struct LockfileError {
    std::string message; ///< Error explanation.
};

/**
 * @brief  Serialize a resolved plan + context as lockfile JSON text.
 *
 * @param[in] plan      Resolved plan whose steps are pinned.
 * @param[in] tc        Toolchain (its target triple is recorded as context).
 * @param[in] roots     Top-level selected root names (context).
 * @param[in] providers Explicit provider bindings, capability -> package (context).
 *
 * @return     Indented lockfile JSON.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_lockfile
 */
std::string serializeLockfile(const BuildPlan& plan, const ToolchainConfig& tc,
                              const std::vector<std::string>& roots,
                              const std::map<std::string, std::string>& providers);

/**
 * @brief  Parse lockfile text.
 *
 * @param[in]  text   JSON text to parse.
 * @param[out] errors Collected parse errors.
 *
 * @return     Parsed lockfile, or nullopt on a fatal JSON syntax error.
 *
 * @throws     None.
 * @note       Non-fatal errors (e.g. an unparseable step version) are appended
 *             to @p errors and the offending step is skipped.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_lockfile
 */
std::optional<Lockfile> parseLockfileText(std::string_view text,
                                          std::vector<LockfileError>& errors);

/**
 * @brief  Read and parse a lockfile from disk.
 *
 * @param[in]  file   Path to the lockfile.
 * @param[out] errors Collected parse errors.
 *
 * @return     Parsed lockfile, or nullopt on a fatal error.
 *
 * @throws     None.
 * @note       Non-fatal errors are appended to @p errors.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_lockfile
 */
std::optional<Lockfile> parseLockfile(const std::filesystem::path& file,
                                      std::vector<LockfileError>& errors);

/**
 * @brief  Serialize and write a lockfile to disk (creates parent directories).
 *
 * @param[in] file      Destination path.
 * @param[in] plan      Resolved plan to pin.
 * @param[in] tc        Toolchain (context).
 * @param[in] roots     Root selections (context).
 * @param[in] providers Provider bindings (context).
 *
 * @return     true on success.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_lockfile
 */
bool writeLockfile(const std::filesystem::path& file, const BuildPlan& plan,
                   const ToolchainConfig& tc, const std::vector<std::string>& roots,
                   const std::map<std::string, std::string>& providers);

/**
 * @brief  Drift between a freshly-resolved plan and a locked plan.
 *
 * @since      0.1
 * @ingroup    lightroot_lockfile
 */
struct LockfileDrift {
    std::vector<std::string> added;   ///< In the plan but not locked ("name@ver").
    std::vector<std::string> removed; ///< Locked but not in the plan.
    std::vector<std::string> changed; ///< Present in both with a different version.

    /**
     * @brief  Whether there is no drift.
     *
     * @return     true iff added, removed, and changed are all empty.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_lockfile
     */
    [[nodiscard]] bool empty() const;

    /**
     * @brief  A human-readable, multi-line summary of the drift.
     *
     * @return     One line per change (empty if no drift).
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_lockfile
     */
    [[nodiscard]] std::string summary() const;
};

/**
 * @brief  Compare a freshly-resolved plan against a lockfile.
 *
 * @details Steps are matched by (scope, name); a version difference, a missing
 *          step, or an extra step all count as drift.
 *
 * @param[in] lock The locked plan.
 * @param[in] plan The freshly-resolved plan.
 *
 * @return     The drift (empty iff the closure matches exactly).
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_lockfile
 */
LockfileDrift checkLockfileDrift(const Lockfile& lock, const BuildPlan& plan);

} // namespace lightroot
