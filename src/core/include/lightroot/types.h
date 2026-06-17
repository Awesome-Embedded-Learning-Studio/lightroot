/**
 * @file    types.h
 * @author  Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief   Core enum types shared across the resolver and exec layers.
 * @version 0.1
 * @date    2026-06-16
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Defines the small enumeration types (Scope, DepKind, ProviderCardinality)
 * used throughout lightroot, plus string conversions for diagnostics and
 * serialization.
 *
 * @ingroup lightroot_types
 */
#pragma once

#include <cstdint>

namespace lightroot {

/**
 * @brief  Where a package lives: built/runs on the host, or installed into the
 *         target rootfs.
 *
 * @details Mirrors Buildroot's host-/target- split.
 *
 * @since      0.1
 * @ingroup    lightroot_types
 */
enum class Scope : std::uint8_t {
    Host,  ///< Built/runs on the build host.
    Target ///< Installed into the target rootfs.
};

/**
 * @brief  Whether a dependency refers to a concrete package or to a virtual
 *         capability that some package must provide (e.g. `virtual:ssl`).
 *
 * @since      0.1
 * @ingroup    lightroot_types
 */
enum class DepKind : std::uint8_t {
    Package, ///< A concrete package by name.
    Virtual  ///< A virtual capability, satisfied by any provider.
};

/**
 * @brief  How many packages may satisfy a virtual capability at once.
 *
 * @since      0.1
 * @ingroup    lightroot_types
 */
enum class ProviderCardinality : std::uint8_t {
    Exclusive, ///< Exactly one provider (e.g. virtual:libc).
    Additive   ///< Zero or more providers (e.g. virtual:firmware).
};

/**
 * @brief  Stringify a Scope value.
 *
 * @param[in] s Scope value to convert.
 *
 * @return     Lowercase canonical name ("host" or "target").
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_types
 */
// NOLINTNEXTLINE
const char* to_string(Scope s);

/**
 * @brief  Stringify a DepKind value.
 *
 * @param[in] k Dependency kind to convert.
 *
 * @return     Lowercase canonical name ("package" or "virtual").
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_types
 */
// NOLINTNEXTLINE
const char* to_string(DepKind k);

/**
 * @brief  Stringify a ProviderCardinality value.
 *
 * @param[in] c Cardinality to convert.
 *
 * @return     Lowercase canonical name ("exclusive" or "additive").
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_types
 */
// NOLINTNEXTLINE
const char* to_string(ProviderCardinality c);

} // namespace lightroot
