/**
 * @file    doctor.h
 * @author  Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief   Toolchain and host readiness checks (`lightroot doctor`).
 * @version 0.1
 * @date    2026-06-16
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Verifies an external cross toolchain (compiler/linker/archiver/strip), the
 * configured target triple, the sysroot, a small static compile, and the host
 * tools needed by the build pipeline — without throwing. This is the pre-flight
 * gate the executor relies on so a misconfigured toolchain is diagnosed clearly
 * rather than surfacing as confusing build failures. Subprocess work reuses the
 * exec primitives; the public checks are exposed individually for unit testing.
 *
 * @ingroup lightroot_doctor
 */
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <lightroot/config.h>

namespace lightroot {

/**
 * @brief  Outcome of a single doctor check.
 *
 * @since      0.1
 * @ingroup    lightroot_doctor
 */
enum class CheckStatus { Pass, Warn, Fail };

/**
 * @brief  Stringify a CheckStatus.
 *
 * @param[in] s Status to convert.
 *
 * @return     Uppercase canonical name ("PASS", "WARN", "FAIL").
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_doctor
 */
// NOLINTNEXTLINE
const char* to_string(CheckStatus s);

/**
 * @brief  Result of one doctor check.
 *
 * @since      0.1
 * @ingroup    lightroot_doctor
 */
struct CheckResult {
    std::string name;                       ///< Short check name, e.g. "compiler".
    CheckStatus status = CheckStatus::Pass; ///< Outcome.
    std::string detail;                     ///< Version string, resolved path, or reason.
};

/**
 * @brief  Aggregate of all doctor checks.
 *
 * @since      0.1
 * @ingroup    lightroot_doctor
 */
struct DoctorReport {
    std::vector<CheckResult> checks; ///< Individual check results, in run order.

    /**
     * @brief  Whether every check passed (no FAIL).
     *
     * @return     true iff no check has status Fail (warnings are tolerated).
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_doctor
     */
    [[nodiscard]] bool ok() const;
};

/**
 * @brief  Tunable options for runDoctor().
 *
 * @since      0.1
 * @ingroup    lightroot_doctor
 */
struct DoctorOptions {
    /// Host tools that must be resolvable on PATH (e.g. make, tar, patch).
    std::vector<std::string> hostTools{"make", "ninja", "tar", "patch"};

    bool skipSmoke = false; ///< Skip the static compile smoke test.
};

/**
 * @brief  Resolve a cross tool binary `{prefix}{tool}`.
 *
 * @details Looks under @p tc.path first, then falls back to PATH search
 *          (existence + executable bit). Used by the tool checks and reusable
 *          by later build phases.
 *
 * @param[in] tc   Toolchain config (prefix + bin path).
 * @param[in] tool Bare tool name, e.g. "gcc" (the prefix is applied).
 *
 * @return     Resolved path, or nullopt if not found.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_doctor
 */
std::optional<std::filesystem::path> findTool(const ToolchainConfig& tc, std::string_view tool);

/**
 * @brief  Check the core cross toolchain binaries exist.
 *
 * @details Probes gcc, g++, ar, strip, ld via findTool() and captures the
 *          compiler's `--version`. FAILs if any are missing.
 *
 * @param[in] tc Toolchain config.
 *
 * @return     Aggregated check result named "tools".
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_doctor
 */
CheckResult checkTools(const ToolchainConfig& tc);

/**
 * @brief  Check the compiler target triple matches the config.
 *
 * @details Runs `{prefix}gcc -dumpmachine` and compares to @p tc.target.
 *
 * @param[in] tc Toolchain config.
 *
 * @return     Check result named "target"; WARN when target is unspecified.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_doctor
 */
CheckResult checkTarget(const ToolchainConfig& tc);

/**
 * @brief  Check the sysroot directory exists.
 *
 * @details When @p tc.sysroot is empty, falls back to
 *          `{prefix}gcc --print-sysroot` and warns that it was unspecified.
 *
 * @param[in] tc Toolchain config.
 *
 * @return     Check result named "sysroot".
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_doctor
 */
CheckResult checkSysroot(const ToolchainConfig& tc);

/**
 * @brief  Static-compile a tiny C program and verify the output ELF arch.
 *
 * @details Writes `int main(void){return 0;}` to a temp dir, compiles it with
 *          `{prefix}gcc --static [--sysroot=tc.sysroot]`, and parses the output
 *          ELF `e_machine` to confirm the target architecture. The temp dir is
 *          removed via ScopeGuard on every return path.
 *
 * @param[in] tc Toolchain config.
 *
 * @return     Check result named "compile-smoke"; WARN if the binary is not the
 *             expected architecture despite compiling.
 *
 * @throws     None.
 * @note       Honors DoctorOptions::skipSmoke at the runDoctor() level.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_doctor
 */
CheckResult checkCompileSmoke(const ToolchainConfig& tc);

/**
 * @brief  Static-compile a tiny C++ program with the cross g++.
 *
 * @details Compiles a program using the C++ standard library (std::string)
 *          with `{prefix}g++ --static [--sysroot=tc.sysroot]` and parses the
 *          output ELF `e_machine` to confirm the target architecture. Catches a
 *          missing libstdc++ in the sysroot that the existence check would miss.
 *
 * @param[in] tc Toolchain config.
 *
 * @return     Check result named "c++-smoke".
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_doctor
 */
CheckResult checkCppSmoke(const ToolchainConfig& tc);

/**
 * @brief  Check the cross strip actually reduces a binary.
 *
 * @details Builds a small static binary, runs `{prefix}strip` on it, and
 *          confirms strip exits cleanly and the output is smaller. Proves strip
 *          is functional, not merely present.
 *
 * @param[in] tc Toolchain config.
 *
 * @return     Check result named "strip".
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_doctor
 */
CheckResult checkStrip(const ToolchainConfig& tc);

/**
 * @brief  Check the host tools required by the build pipeline.
 *
 * @details Each tool in @p opts.hostTools must resolve on PATH; additionally a
 *          fetch tool (curl or wget) is required and `qemu-aarch64` is reported
 *          as a warning when absent (only needed for Stage 4 rootfs validation).
 *
 * @param[in] opts Doctor options (carries the host tool list).
 *
 * @return     Check result named "host-tools".
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_doctor
 */
CheckResult checkHostTools(const DoctorOptions& opts);

/**
 * @brief  Run all doctor checks for a toolchain config.
 *
 * @param[in] tc   Toolchain config to validate.
 * @param[in] opts Tunable options (skip smoke, host tool list).
 *
 * @return     Aggregate report; ok() is true iff no check FAILed.
 *
 * @throws     None.
 * @note       Checks run in order: tools, target, sysroot, compile-smoke,
 *             c++-smoke, strip, host-tools. The three smoke checks
 *             (compile-smoke, c++-smoke, strip) are skipped when opts.skipSmoke
 *             is set.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_doctor
 */
DoctorReport runDoctor(const ToolchainConfig& tc, const DoctorOptions& opts = {});

} // namespace lightroot
