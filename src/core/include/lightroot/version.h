/**
 * @file    version.h
 * @author  Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief   Version numbers, constraints, and parsing.
 * @version 0.1
 * @date    2026-06-16
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Provides a simple dotted numeric version (major.minor.patch), a comparator
 * with a bound (VersionConstraint), and parsers for both from text. Parsers
 * return lightroot::expected so a failure carries a reason, not just nullopt.
 *
 * @ingroup lightroot_version
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <lightroot/expected.h>

namespace lightroot {

/**
 * @brief  A simple dotted numeric version, major.minor.patch.
 *
 * @since      0.1
 * @ingroup    lightroot_version
 */
struct Version {
    std::uint32_t major = 0; ///< Major component.
    std::uint32_t minor = 0; ///< Minor component.
    std::uint32_t patch = 0; ///< Patch component.

    /**
     * @brief  Three-way comparison against another version.
     *
     * @param[in] o Version to compare against.
     *
     * @return     -1 if *this < @p o, 0 if equal, 1 if greater.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_version
     */
    [[nodiscard]] int compare(const Version& o) const;

    /**
     * @brief  Render the version as "major.minor.patch".
     *
     * @return     Dotted textual form.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_version
     */
    // NOLINTNEXTLINE
    [[nodiscard]] std::string to_string() const;
};

/**
 * @brief  Parse a dotted numeric version.
 *
 * @param[in] s Text to parse ("3", "3.0", or "3.0.1").
 *
 * @return     expected holding the Version, or an error message on failure.
 *
 * @throws     None.
 * @note       Missing components default to 0.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_version
 */
expected<Version, std::string> parseVersion(std::string_view s);

/**
 * @brief  Version-comparison operators used by VersionConstraint.
 *
 * @since      0.1
 * @ingroup    lightroot_version
 */
enum class Cmp : std::uint8_t {
    Any, ///< No constraint.
    Eq,  ///< Equal.
    Ne,  ///< Not equal.
    Ge,  ///< Greater-or-equal.
    Gt,  ///< Greater-than.
    Le,  ///< Less-or-equal.
    Lt   ///< Less-than.
};

/**
 * @brief  A version comparator plus bound, e.g. `>= 3.0`.
 *
 * @since      0.1
 * @ingroup    lightroot_version
 */
struct VersionConstraint {
    Cmp cmp = Cmp::Any; ///< Comparison operator.
    Version ver;        ///< Bound version.

    /**
     * @brief  Test whether a version satisfies this constraint.
     *
     * @param[in] v Version to test.
     *
     * @return     true if @p v meets the comparator/bound.
     *
     * @throws     None.
     * @note       Cmp::Any always satisfies.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_version
     */
    [[nodiscard]] bool satisfies(const Version& v) const;

    /**
     * @brief  Render the constraint as "OP version".
     *
     * @return     Textual form such as ">= 3.0", or "" for Cmp::Any.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_version
     */
    // NOLINTNEXTLINE
    [[nodiscard]] std::string to_string() const;
};

/**
 * @brief  Build a VersionConstraint from an operator and a version string.
 *
 * @param[in] op          Operator: "=", "==", "!=", ">=", ">", "<=", "<".
 * @param[in] version_str Dotted version text, e.g. "3.0".
 *
 * @return     expected holding the constraint, or an error message on failure.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_version
 */
expected<VersionConstraint, std::string> parseVersionConstraint(std::string_view op,
                                                                std::string_view version_str);

} // namespace lightroot
