/**
 * @file    diagnostic.h
 * @author  Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief   Structured diagnostics emitted by the resolver and executor.
 * @version 0.1
 * @date    2026-06-16
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Defines the Diagnostic record and severity used to report resolution and
 * build problems without throwing. Codes are machine-readable for tooling;
 * capability/providers carry optional context for richer rendering.
 *
 * @ingroup lightroot_diagnostic
 */
#pragma once

#include <string>
#include <vector>

namespace lightroot {

/**
 * @brief  Severity of a Diagnostic.
 *
 * @since      0.1
 * @ingroup    lightroot_diagnostic
 */
enum class DiagSeverity { Info, Warning, Error };

/**
 * @brief  Stringify a DiagSeverity.
 *
 * @param[in] s Severity to convert.
 *
 * @return     Lowercase canonical name ("info", "warning", "error").
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_diagnostic
 */
// NOLINTNEXTLINE
inline const char* to_string(DiagSeverity s) {
    switch (s) {
        case DiagSeverity::Info:
            return "info";
        case DiagSeverity::Warning:
            return "warning";
        case DiagSeverity::Error:
            return "error";
    }
    return "error";
}

/**
 * @brief  A structured diagnostic emitted by the resolver (and later the
 *         executor).
 *
 * @details @c code is machine-readable for tooling; @c capability and @c
 *          providers carry optional provider-context for richer rendering.
 *
 * @since      0.1
 * @ingroup    lightroot_diagnostic
 */
struct Diagnostic {
    DiagSeverity severity = DiagSeverity::Error; ///< Severity level.
    std::string code;                            ///< Machine-readable code, e.g. "missing-package".
    std::string message;                         ///< Human-readable explanation.
    std::string capability;                      ///< Optional capability, e.g. "ssl".
    std::vector<std::string> providers;          ///< Optional provider list.
};

/**
 * @brief  Whether a diagnostic is error-severity.
 *
 * @param[in] d Diagnostic to test.
 *
 * @return     true if @p d is DiagSeverity::Error.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_diagnostic
 */
inline bool isError(const Diagnostic& d) {
    return d.severity == DiagSeverity::Error;
}

} // namespace lightroot
