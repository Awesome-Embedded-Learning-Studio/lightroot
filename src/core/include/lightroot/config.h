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
 * @brief  One file written into a partition filesystem.
 *
 * @details Exactly one of @c src (an artifact exported to @c $LIGHTROOT_OUT, e.g.
 *          "zImage") or @c inline_text (a generated file body, e.g. boot.cmd) is
 *          set. The C++ executor treats both opaquely; it knows nothing about
 *          what the file IS for any given board.
 *
 * @since      0.1
 * @ingroup    lightroot_config
 */
struct ImageFile {
    std::string dst;         ///< Destination path inside the partition; basename of src if empty.
    std::string src;         ///< Artifact name in @c $LIGHTROOT_OUT; empty if inline_text.
    std::string inline_text; ///< Literal file body; empty if src.
};

/**
 * @brief  One partition in a data-described disk image.
 *
 * @details Board-agnostic: the executor renders any partition table / filesystem
 *          layout described by a list of these. @c start_mib places the partition
 *          (MiB); @c size is a decimal MiB count, @c "auto" (rootfs usage +
 *          headroom, only for a @c rootfs partition), or @c "rest" (fill to the
 *          end). A @c rootfs partition is filled from the assembled target tree;
 *          any other is filled from @c files.
 *
 * @since      0.1
 * @ingroup    lightroot_config
 */
struct ImagePartition {
    unsigned start_mib = 0;        ///< Partition start offset in MiB.
    std::string size = "rest";     ///< "<N>" MiB | "auto" | "rest".
    unsigned type = 0x83;          ///< MBR partition type (rendered as hex).
    bool bootable = false;         ///< Mark the partition bootable in the MBR.
    bool rootfs = false;           ///< Fill from the assembled target rootfs tree.
    std::string fs = "ext4";       ///< Filesystem (currently "ext4").
    std::string label;             ///< Filesystem label.
    std::vector<ImageFile> files;  ///< Files for non-rootfs partitions.
};

/**
 * @brief  An optional pre-partition payload, e.g. a bootloader at a fixed offset.
 *
 * @since      0.1
 * @ingroup    lightroot_config
 */
struct ImagePayload {
    std::string file;       ///< Artifact name in @c $LIGHTROOT_OUT (e.g. a bootloader image).
    unsigned offset_kib = 0; ///< Byte offset (KiB) within the image to write it.
};

/**
 * @brief  A partitioned disk image fully described by config data.
 *
 * @details The executor renders this verbatim into a flashable image
 *          (payload + MBR partition table + per-partition filesystems). It
 *          contains NO board-specific knowledge — any board expresses its layout
 *          as an [image] table in its project config.
 *
 * @since      0.1
 * @ingroup    lightroot_config
 */
struct PartitionedImage {
    std::string output;                   ///< Output filename (e.g. "sdcard.img").
    std::optional<ImagePayload> payload;  ///< Optional pre-partition payload.
    std::vector<ImagePartition> partitions; ///< Partitions, in table order.
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

    /// Optional partitioned disk image layout ([image] table). When set, the
    /// assemble phase renders a flashable image from this data description.
    std::optional<PartitionedImage> image;
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
