/**
 * @file    manifest.h
 * @author  Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief   Package manifest parsing from TOML.
 * @version 0.1
 * @date    2026-06-16
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Parses a single package manifest (name, version, provides, depends, source,
 * build) into a typed Package. The registry composes many manifests.
 *
 * @ingroup lightroot_manifest
 */
#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <lightroot/dependency.h>
#include <lightroot/types.h>
#include <lightroot/version.h>

namespace lightroot {

/**
 * @brief  Where a package's source comes from.
 *
 * @since      0.1
 * @ingroup    lightroot_manifest
 */
struct SourceInfo {
    std::string kind;   ///< Source kind, e.g. "archive".
    std::string url;    ///< Fetch URL.
    std::string sha256; ///< Integrity hash.

    /// Patch files to apply after extract, before configure. Paths are relative
    /// to the manifest's directory (resolved like a local source URL). Applied in
    /// order at the `patch` lifecycle phase via `patch -p1 -i <file>`, unless an
    /// explicit `[build.patch].run` overrides the command.
    std::vector<std::string> patches;
};

/**
 * @brief  Shell commands for one lifecycle phase.
 *
 * @details Each string in @c run is executed via `sh -c` in the package's build
 *          directory, so $VAR expansion, pipes, and `&&` are available.
 *
 * @since      0.1
 * @ingroup    lightroot_manifest
 */
struct PhaseCommands {
    std::vector<std::string> run; ///< Shell snippets, run in declared order.
};

/**
 * @brief  How a package is built.
 *
 * @details @c backend selects the strategy ("script" uses the per-phase @c run
 *          commands; later backends like "make"/"cmake" may synthesize defaults).
 *          @c env is merged into every phase's environment; @c phases maps a
 *          phase key ("configure"/"build"/"install_staging"/"install_target") to
 *          its commands.
 *
 * @since      0.1
 * @ingroup    lightroot_manifest
 */
struct BuildInfo {
    std::string backend; ///< Backend, e.g. "script", "make", "cmake".

    /// Extra environment merged into all phases (e.g. CFLAGS = "-O2").
    std::map<std::string, std::string> env;

    /// Phase key -> commands; keys: configure, build, install_staging, install_target.
    std::map<std::string, PhaseCommands> phases;
};

/**
 * @brief  One package manifest, parsed from TOML into typed fields.
 *
 * @since      0.1
 * @ingroup    lightroot_manifest
 */
struct Package {
    std::string name;            ///< Package name.
    Version version;             ///< Package version.
    std::string summary;         ///< Short description.
    std::string license;         ///< License identifier.
    Scope scope = Scope::Target; ///< Host or target package.

    /// Raw capability strings declared by the package, e.g. "virtual:ssl".
    /// Parsed into typed refs lazily by the provider resolver.
    std::vector<std::string> provides;

    std::vector<DependencyRef> depends; ///< Declared dependencies.

    std::optional<SourceInfo> source; ///< Optional source info.
    std::optional<BuildInfo> build;   ///< Optional build info.

    /// Which manifest file this came from (for diagnostics / overlay reporting).
    std::filesystem::path origin;
};

/**
 * @brief  An error raised while parsing a manifest.
 *
 * @since      0.1
 * @ingroup    lightroot_manifest
 */
struct ManifestError {
    std::string message;  ///< Error explanation.
    std::size_t line = 0; ///< 1-based line; 0 when not known.
};

/**
 * @brief  Parse manifest text into a Package.
 *
 * @param[in]  text   TOML text to parse.
 * @param[in]  origin Recorded on the Package for diagnostics.
 * @param[out] errors Collected parse errors.
 *
 * @return     Parsed package, or nullopt on a fatal error.
 *
 * @throws     None.
 * @note       Non-fatal errors are appended to @p errors.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_manifest
 */
std::optional<Package> parseManifestText(std::string_view text, const std::filesystem::path& origin,
                                         std::vector<ManifestError>& errors);

/**
 * @brief  Read and parse a manifest file from disk.
 *
 * @param[in]  file   Path to the manifest TOML file.
 * @param[out] errors Collected parse errors.
 *
 * @return     Parsed package, or nullopt on a fatal error.
 *
 * @throws     None.
 * @note       Non-fatal errors are appended to @p errors.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_manifest
 */
std::optional<Package> parseManifest(const std::filesystem::path& file,
                                     std::vector<ManifestError>& errors);

} // namespace lightroot
