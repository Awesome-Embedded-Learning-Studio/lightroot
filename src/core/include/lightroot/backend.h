/**
 * @file    backend.h
 * @author  Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief   Build-backend knowledge: default lifecycle commands + generated files.
 * @version 0.1
 * @date    2026-06-17
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * A "backend" is the strategy for turning a package's source into installed
 * artifacts: the default per-phase shell commands (configure/build/install) and
 * any files the executor must generate into the workdir before those commands
 * run (e.g. a CMake toolchain file, a Meson cross file). Concentrating this
 * knowledge here keeps the executor backend-agnostic — adding a new backend
 * (cmake, meson, ...) means extending @ref defaultPhases and @ref backendFiles
 * in backend.cpp, with no change to the executor's core loop.
 *
 * @ingroup lightroot_backend
 */
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <lightroot/config.h>
#include <lightroot/manifest.h>

namespace lightroot {

/**
 * @brief  A file the executor writes into a package's workdir for a backend.
 *
 * @details @c filename is relative to the workdir; @c env_var is set (in the
 *          phase environment) to the absolute workdir-relative path so phase
 *          commands can reference it (e.g. @c $LIGHTROOT_CMAKE_TOOLCHAIN).
 *
 * @since      0.1
 * @ingroup    lightroot_backend
 */
struct BackendFile {
    std::string filename; ///< File name within the workdir ("lightroot-toolchain.cmake").
    std::string env_var;  ///< Env var holding the file's absolute path.
    std::string content;  ///< File body (already rendered for the toolchain).
};

/**
 * @brief  Default lifecycle phase commands for a well-known backend.
 *
 * @details Used when a manifest declares a `backend` but no explicit per-phase
 *          `run` commands. Known backends: "make-kconfig", "make", "autotools",
 *          "cmake". Returns an empty map for an unknown backend, in which case
 *          the package only goes through fetch/verify/extract (and patch).
 *
 * @param[in] backend Backend name from the manifest.
 *
 * @return     Phase key -> commands for the backend's canonical lifecycle.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_backend
 */
std::map<std::string, PhaseCommands> defaultPhases(std::string_view backend);

/**
 * @brief  Files a backend needs the executor to generate into the workdir.
 *
 * @details Returns the toolchain/cross files the backend's phase commands
 *          reference. For "cmake" this is a CMake toolchain file (with @c
 *          CMAKE_SYSROOT / @c CMAKE_FIND_ROOT_PATH pointing at the sysroot and
 *          staging so cmake finds the cross libc and already-built deps). For
 *          backends that need nothing ("make", "autotools", "make-kconfig", "")
 *          the result is empty. Generated regardless of whether the manifest
 *          overrides the default phases, so a package with custom commands can
 *          still reference @c $LIGHTROOT_CMAKE_TOOLCHAIN.
 *
 * @param[in] backend Backend name from the manifest.
 * @param[in] tc      Toolchain config (prefix, sysroot, target).
 * @param[in] staging Staging directory (added to the backend's search paths).
 *
 * @return     Rendered files to write into the workdir.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_backend
 */
std::vector<BackendFile> backendFiles(std::string_view backend, const ToolchainConfig& tc,
                                      const std::filesystem::path& staging);

} // namespace lightroot
