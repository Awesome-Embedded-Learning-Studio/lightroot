/**
 * @file    device_table.h
 * @author  Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief   Rootfs device table: directory skeleton, permissions, and device nodes.
 * @version 0.1
 * @date    2026-06-17
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * A device table describes the rootfs nodes the executor must create at assemble
 * time under fakeroot: standard directories with explicit permissions/ownership
 * and character/block device nodes (/dev/null, /dev/console, ...). Without a
 * device table a produced rootfs carries build-user ownership and zero device
 * nodes, which is unusable on real hardware.
 *
 * The table is materialised by the executor (see @ref executor.h): the built-in
 * @ref defaultDeviceTable applies out of the box, and an optional project-level
 * device-table file (referenced from `[rootfs]` in the project config) overrides
 * or extends it, matching entries by path. Parsing is multi-error
 * (`optional<DeviceTable>` + an out-parameter error vector), mirroring the
 * manifest/config parsers.
 *
 * @ingroup lightroot_device_table
 */
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lightroot {

/**
 * @brief  Kind of a device-table entry.
 *
 * @since      0.1
 * @ingroup    lightroot_device_table
 */
enum class DeviceKind {
    Dir,  ///< Directory.
    Char, ///< Character device node (needs major/minor).
    Block ///< Block device node (needs major/minor).
};

/**
 * @brief  Stringify a DeviceKind.
 *
 * @param[in] k Kind to convert.
 *
 * @return     Canonical lowercase name ("dir", "char", "block").
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_device_table
 */
// NOLINTNEXTLINE
const char* to_string(DeviceKind k);

/**
 * @brief  Parse a DeviceKind from its canonical name.
 *
 * @param[in] s Text to parse ("dir" | "char" | "block").
 *
 * @return     The parsed kind, or nullopt if @p s is not recognised.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_device_table
 */
// NOLINTNEXTLINE
std::optional<DeviceKind> parseDeviceKind(std::string_view s);

/**
 * @brief  One entry in a device table.
 *
 * @details A plain aggregate (no trailing-underscore members), matching
 *          @c SourceInfo / @c BuildInfo / @c Package in manifest.h. @c major and
 *          @c minor are only meaningful for Char/Block entries.
 *
 * @since      0.1
 * @ingroup    lightroot_device_table
 */
struct DeviceEntry {
    std::string path;                  ///< Absolute path within the rootfs, e.g. "/dev/null".
    DeviceKind kind = DeviceKind::Dir; ///< Entry kind (Dir/Char/Block).
    unsigned mode = 0755;              ///< Permission bits, e.g. 0666, 0600, 1777.
    unsigned uid = 0;                  ///< Owner uid (almost always 0).
    unsigned gid = 0;                  ///< Owner gid (almost always 0).
    int major = 0;                     ///< Device major number (Char/Block only).
    int minor = 0;                     ///< Device minor number (Char/Block only).
};

/**
 * @brief  An ordered device table.
 *
 * @since      0.1
 * @ingroup    lightroot_device_table
 */
struct DeviceTable {
    std::vector<DeviceEntry> entries; ///< Entries in application order.
};

/**
 * @brief  An error raised while parsing a device table.
 *
 * @since      0.1
 * @ingroup    lightroot_device_table
 */
struct DeviceTableError {
    std::string message;  ///< Error explanation.
    std::size_t line = 0; ///< 1-based line; 0 when not known.
};

/**
 * @brief  Parse device-table text (a `[[device]]` array-of-tables document).
 *
 * @details Each `[[device]]` table has: @c path (required, absolute), @c kind
 *          ("dir"|"char"|"block"), @c mode/@c uid/@c gid (integers), and @c major/
 *          @c minor (required for char/block). Invalid entries push a non-fatal
 *          error and are skipped; the rest still parse.
 *
 * @param[in]  text   TOML text to parse.
 * @param[out] errors Collected parse errors.
 *
 * @return     Parsed table, or nullopt on a fatal TOML syntax error.
 *
 * @throws     None.
 * @note       Non-fatal errors are appended to @p errors.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_device_table
 */
std::optional<DeviceTable> parseDeviceTableText(std::string_view text,
                                                std::vector<DeviceTableError>& errors);

/**
 * @brief  Read and parse a device-table file from disk.
 *
 * @param[in]  file   Path to the device-table TOML file.
 * @param[out] errors Collected parse errors.
 *
 * @return     Parsed table, or nullopt on a fatal error.
 *
 * @throws     None.
 * @note       Non-fatal errors are appended to @p errors.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_device_table
 */
std::optional<DeviceTable> parseDeviceTable(const std::filesystem::path& file,
                                            std::vector<DeviceTableError>& errors);

/**
 * @brief  The built-in minimal Linux device table.
 *
 * @details Standard rootfs directories with correct permissions (/tmp 1777,
 *          /root 0700, /proc /sys 0555, ...) and the essential device nodes
 *          (/dev/null, /dev/zero, /dev/console, /dev/tty, /dev/random,
 *          /dev/urandom). Applied by default so a rootfs is hardware-correct
 *          with zero configuration — a usability advantage over Buildroot.
 *
 * @return     A device table with the canonical minimal entries.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_device_table
 */
DeviceTable defaultDeviceTable();

/**
 * @brief  Merge additions into a device table, matching by path.
 *
 * @details An addition whose @c path equals an existing entry replaces it;
 *          otherwise it is appended. Used to overlay a project device-table file
 *          onto the built-in default.
 *
 * @param[in,out] table     Table to merge into.
 * @param[in]     additions Entries to overlay (later entries win).
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_device_table
 */
void mergeDeviceEntries(DeviceTable& table, const std::vector<DeviceEntry>& additions);

/**
 * @brief  Resolve the final device table for a project.
 *
 * @details Starts from @ref defaultDeviceTable, then overlays the device-table
 *          file at @p device_table_file (resolved against @p config_dir, if
 *          relative) with same-path entries overriding the defaults. Parse errors
 *          for the file are appended to @p errors; an empty @p device_table_file
 *          or an unparsable file leaves the default table intact.
 *
 * @param[in] device_table_file Optional device-table file path (may be empty).
 * @param[in] config_dir        Directory to resolve a relative device-table path
 *                              against (typically the project config's directory).
 * @param[out] errors           Collected parse errors.
 *
 * @return     The materialised device table (never empty).
 *
 * @throws     None.
 * @note       Non-fatal errors are appended to @p errors.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_device_table
 */
DeviceTable resolveDeviceTable(const std::filesystem::path& device_table_file,
                               const std::filesystem::path& config_dir,
                               std::vector<DeviceTableError>& errors);

} // namespace lightroot
