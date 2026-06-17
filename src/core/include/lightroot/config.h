/**
 * @file    config.h
 * @author  Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief   Project configuration parsing from TOML.
 * @version 0.1
 * @date    2026-06-16
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Parses the project's lightroot.toml (toolchain, profile, provider
 * selections, build selections, output directory) into typed config structs.
 *
 * @ingroup lightroot_config
 */
#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lightroot {

/**
 * @brief  External toolchain configuration.
 *
 * @details The first stage supports only kind = "external".
 *
 * @since      0.1
 * @ingroup    lightroot_config
 */
struct ToolchainConfig {
    std::string kind = "external"; ///< Toolchain kind; first stage only "external".
    std::string prefix;            ///< Cross prefix, e.g. "aarch64-linux-gnu-".
    std::string path;              ///< Bin directory.
    std::string sysroot;           ///< Sysroot path.
    std::string target;            ///< Target triple, e.g. "aarch64-linux-gnu".
};

/**
 * @brief  Parsed project configuration.
 *
 * @since      0.1
 * @ingroup    lightroot_config
 */
struct ProjectConfig {
    ToolchainConfig toolchain; ///< Toolchain settings.
    std::string profile;       ///< Profile name, e.g. "minimal".

    /// Explicit provider selections: "virtual:ssl" -> "openssl".
    std::map<std::string, std::string> providers;

    /// Package names selected to be built.
    std::vector<std::string> selected;

    std::filesystem::path output_dir = "out"; ///< Build output directory.

    /// Rootfs image formats to produce ("tar", "cpio", "ext4"). Default ["tar"].
    std::vector<std::string> image_formats{"tar"};

    /// Optional device-table file (relative to the project config dir). When
    /// empty, the built-in default device table applies. @see device_table.h.
    std::filesystem::path device_table_file;
};

/**
 * @brief  An error raised while parsing project config.
 *
 * @since      0.1
 * @ingroup    lightroot_config
 */
struct ConfigError {
    std::string message;  ///< Error explanation.
    std::size_t line = 0; ///< 1-based line; 0 when unknown.
};

/**
 * @brief  Parse project config from text.
 *
 * @param[in]  text   TOML text to parse.
 * @param[out] errors Collected parse errors.
 *
 * @return     Parsed config, or nullopt on a fatal error.
 *
 * @throws     None.
 * @note       Non-fatal errors are appended to @p errors.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_config
 */
std::optional<ProjectConfig> parseConfigText(std::string_view text,
                                             std::vector<ConfigError>& errors);

/**
 * @brief  Read and parse a project config file from disk.
 *
 * @param[in]  file   Path to the TOML file.
 * @param[out] errors Collected parse errors.
 *
 * @return     Parsed config, or nullopt on a fatal error.
 *
 * @throws     None.
 * @note       Non-fatal errors are appended to @p errors.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_config
 */
std::optional<ProjectConfig> parseConfig(const std::filesystem::path& file,
                                         std::vector<ConfigError>& errors);

} // namespace lightroot
