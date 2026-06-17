/**
 * @file    resolver.h
 * @author  Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief   Dependency resolution and build-plan generation.
 * @version 0.1
 * @date    2026-06-16
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Resolves a set of root selections against a PackageRegistry into a
 * topologically-ordered BuildPlan, honoring provider semantics and emitting
 * structured diagnostics instead of throwing.
 *
 * @ingroup lightroot_resolver
 */
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <lightroot/dependency.h>
#include <lightroot/diagnostic.h>
#include <lightroot/manifest.h>
#include <lightroot/registry.h>
#include <lightroot/types.h>
#include <lightroot/version.h>

namespace lightroot {

/**
 * @brief  One resolved build step, in dependency order.
 *
 * @since      0.1
 * @ingroup    lightroot_resolver
 */
struct BuildStep {
    Scope scope;                        ///< Host or target scope.
    std::string name;                   ///< Package name.
    Version version;                    ///< Resolved version.
    std::string backend;                ///< Build backend, e.g. "make-kconfig".
    std::vector<std::string> provides;  ///< Capabilities provided.
    std::vector<DependencyRef> depends; ///< Dependencies.
};

/**
 * @brief  The full resolved plan: topologically-ordered steps plus diagnostics.
 *
 * @since      0.1
 * @ingroup    lightroot_resolver
 */
struct BuildPlan {
    std::vector<BuildStep> steps;        ///< Steps with deps before dependents.
    std::vector<Diagnostic> diagnostics; ///< Problems found during resolution.

    /**
     * @brief  Whether the plan has no error-severity diagnostics.
     *
     * @return     true if no Diagnostic is DiagSeverity::Error.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_resolver
     */
    [[nodiscard]] bool ok() const;
};

/**
 * @brief  Resolves a set of root selections against a registry into a build plan.
 *
 * @details Provider semantics: explicit selection wins; a single provider is
 *          auto-selected; zero or ambiguous providers are diagnostics, never a
 *          silent guess; an exclusive capability with two providers in the
 *          closure is a conflict.
 *
 * @since      0.1
 * @ingroup    lightroot_resolver
 */
class Resolver {
  public:
    /**
     * @brief  Construct a resolver bound to a registry.
     *
     * @param[in] reg                Registry to resolve against (referenced, not owned).
     * @param[in] explicit_providers Maps a capability name (e.g. "ssl", no
     *                               "virtual:" prefix) to a package name.
     *
     * @throws     None.
     * @note       The registry must outlive the resolver.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_resolver
     */
    Resolver(const PackageRegistry& reg, std::map<std::string, std::string> explicit_providers);

    /**
     * @brief  Resolve a set of root selections into a build plan.
     *
     * @param[in] roots Root selections to resolve.
     *
     * @return     The resolved plan (steps in dependency order) with diagnostics.
     *
     * @throws     None.
     * @note       Problems are reported as diagnostics, not exceptions.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_resolver
     */
    BuildPlan resolve(const std::vector<DependencyRef>& roots);

  private:
    using Key = std::pair<Scope, std::string>;

    enum class Mark { Unvisited = 0, Visiting, Done };

    const PackageRegistry& reg_;
    std::map<std::string, std::string> explicit_;       ///< Capability -> package.
    std::map<std::string, std::vector<Key>> providers_; ///< Capability -> providers.

    std::map<Key, Mark> marks_;
    std::map<std::string, Key> resolved_caps_; ///< Memoized provider choice per capability.
    std::vector<BuildStep> order_;
    std::vector<Diagnostic> diags_;

    void indexProviders();
    std::optional<Key> resolveDep(const DependencyRef& dep);
    std::optional<Key> resolveVirtual(std::string_view cap);
    void visit(Scope scope, std::string_view name, std::vector<Key>& path);
    void scanConflicts();
    void diag(Diagnostic d);
};

/**
 * @brief  Emit a build plan as pretty-printed JSON.
 *
 * @param[in] plan Plan to serialize.
 *
 * @return     Indented JSON string.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_resolver
 */
// NOLINTNEXTLINE
std::string to_json(const BuildPlan& plan);

} // namespace lightroot
