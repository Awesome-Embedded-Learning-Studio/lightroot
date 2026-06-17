/**
 * @file    dependency.h
 * @author  Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief   Dependency-spec parsing and typed dependency references.
 * @version 0.1
 * @date    2026-06-16
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Parses textual dependency specs (e.g. "target:wget (>=1.21)") into typed
 * DependencyRef values, the unit the resolver operates on.
 *
 * @ingroup lightroot_dependency
 */
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <lightroot/expected.h>
#include <lightroot/types.h>
#include <lightroot/version.h>

namespace lightroot {

/**
 * @brief  Diagnostics produced by the dependency-spec parser.
 *
 * @details @c column is 1-based; the line is supplied by the caller (the
 *          manifest/config parser knows it).
 *
 * @since      0.1
 * @ingroup    lightroot_dependency
 */
struct ParseError {
    std::string message;    ///< Error explanation.
    std::size_t column = 0; ///< 1-based column, 0 means "no position".
};

/**
 * @brief  A typed dependency reference — the unit the resolver works on.
 *
 * @details Produced by parsing a string spec.
 *
 * @since      0.1
 * @ingroup    lightroot_dependency
 */
struct DependencyRef {
    Scope scope = Scope::Target;       ///< Host or target scope.
    DepKind kind = DepKind::Package;   ///< Concrete package or virtual capability.
    std::string name;                  ///< Package or capability name.
    VersionConstraint version;         ///< Optional version constraint.
    std::vector<std::string> features; ///< Optional feature flags.

    /**
     * @brief  Render the ref back into its spec form.
     *
     * @return     Textual spec such as "target:wget (>= 1.21)".
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_dependency
     */
    // NOLINTNEXTLINE
    [[nodiscard]] std::string to_string() const;
};

/**
 * @brief  Parse a dependency spec into a typed DependencyRef.
 *
 * @param[in] spec Textual spec to parse.
 *
 * @return     expected holding the DependencyRef, or a ParseError on failure.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_dependency
 */
expected<DependencyRef, ParseError> parseDependency(std::string_view spec);

} // namespace lightroot
