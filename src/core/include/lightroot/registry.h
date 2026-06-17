/**
 * @file    registry.h
 * @author  Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief   Package index with overlay-merge support.
 * @version 0.1
 * @date    2026-06-16
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * PackageRegistry indexes parsed packages by (scope, name) and supports
 * deterministic overlay merges: a later package with the same key overrides the
 * earlier one, and the override is recorded as a Duplicate for diagnostics.
 *
 * @ingroup lightroot_registry
 */
#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <lightroot/manifest.h>
#include <lightroot/types.h>

namespace lightroot {

/**
 * @brief  Indexes parsed packages by (scope, name) with overlay semantics.
 *
 * @details A later package with the same key overrides the earlier one and the
 *          override is recorded as a Duplicate for diagnostics.
 *
 * @since      0.1
 * @ingroup    lightroot_registry
 */
class PackageRegistry {
  public:
    /**
     * @brief  Records one (scope, name) key defined more than once.
     *
     * @since      0.1
     * @ingroup    lightroot_registry
     */
    struct Duplicate {
        Scope scope;                  ///< Scope of the duplicated key.
        std::string name;             ///< Name of the duplicated key.
        std::filesystem::path first;  ///< Origin of the original definition.
        std::filesystem::path second; ///< Origin of the overriding definition.
    };

    /**
     * @brief  Add a package.
     *
     * @param[in] pkg Package to add (moved in).
     *
     * @throws     None.
     * @note       On a duplicate (scope+name) the new package overrides and a
     *             Duplicate is recorded.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_registry
     */
    void add(Package pkg);

    /**
     * @brief  Look up a package by scope and name.
     *
     * @param[in] scope Scope to search.
     * @param[in] name  Package name.
     *
     * @return     Pointer to the package, or nullptr if absent.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_registry
     */
    [[nodiscard]] const Package* find(Scope scope, std::string_view name) const;

    /**
     * @brief  Look up a target-scope package by name.
     *
     * @param[in] name Package name.
     *
     * @return     Pointer to the package, or nullptr if absent.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_registry
     */
    [[nodiscard]] const Package* findTarget(std::string_view name) const;

    /**
     * @brief  Look up a host-scope package by name.
     *
     * @param[in] name Package name.
     *
     * @return     Pointer to the package, or nullptr if absent.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_registry
     */
    [[nodiscard]] const Package* findHost(std::string_view name) const;

    /**
     * @brief  Number of indexed packages (after overrides).
     *
     * @return     Package count.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_registry
     */
    [[nodiscard]] std::size_t size() const { return order_.size(); }

    /**
     * @brief  Recorded duplicates from overlay merges.
     *
     * @return     Const reference to the duplicate list.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_registry
     */
    [[nodiscard]] const std::vector<Duplicate>& duplicates() const { return dups_; }

    /**
     * @brief  All packages in insertion order (after overrides applied).
     *
     * @return     Const reference to the ordered package vector.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_registry
     */
    [[nodiscard]] const std::vector<Package>& packages() const { return order_; }

  private:
    using Key = std::pair<Scope, std::string>;
    std::map<Key, std::size_t> index_; ///< Key -> position in order_.
    std::vector<Package> order_;       ///< Packages in insertion order.
    std::vector<Duplicate> dups_;      ///< Recorded overrides.
};

} // namespace lightroot
