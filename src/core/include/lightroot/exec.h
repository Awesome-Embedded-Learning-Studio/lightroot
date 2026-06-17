/**
 * @file    exec.h
 * @author  Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief   External command execution and JSONL event logging.
 * @version 0.1
 * @date    2026-06-16
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Runs child processes (fork/exec) with stdout+stderr capture, and provides an
 * append-only JSONL event log for the build pipeline. File descriptors are
 * managed via UniqueFd so every return path cleans up.
 *
 * @ingroup lightroot_exec
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lightroot {

/**
 * @brief  Result of running an external command.
 *
 * @since      0.1
 * @ingroup    lightroot_exec
 */
struct CommandResult {
    int exit_code = -1;    ///< Process exit code, or -1 if it did not exit normally.
    bool launched = false; ///< Whether the process was successfully launched.

    /**
     * @brief  Whether the command launched and exited 0.
     *
     * @return     true if launched and exit_code == 0.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_exec
     */
    [[nodiscard]] bool ok() const { return launched && exit_code == 0; }
};

/**
 * @brief  Run `argv` in `cwd`, streaming combined stdout+stderr to `log_path`.
 *
 * @param[in] argv     Command vector (argv[0] is the program).
 * @param[in] cwd      Working directory (empty for the current directory).
 * @param[in] log_path File to truncate and write combined output to.
 * @param[in] env      Extra environment (KEY,VALUE); when non-empty the child
 *                     runs with inherited `environ` overlaid with these pairs.
 *
 * @return     CommandResult; launched is false on spawn failure.
 *
 * @throws     None.
 * @note       Each call truncates the log.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_exec
 */
CommandResult runCommand(const std::vector<std::string>& argv, const std::filesystem::path& cwd,
                         const std::filesystem::path& log_path,
                         const std::vector<std::pair<std::string, std::string>>& env = {});

/**
 * @brief  Run `argv` and capture combined stdout+stderr into a string.
 *
 * @param[in]  argv     Command vector (argv[0] is the program).
 * @param[in]  cwd      Working directory (empty for the current directory).
 * @param[out] captured Filled with combined child output.
 * @param[in]  env      Extra environment (KEY,VALUE); when non-empty the child
 *                      runs with inherited `environ` overlaid with these pairs.
 *
 * @return     CommandResult; launched is false on spawn failure.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_exec
 */
CommandResult runCapture(const std::vector<std::string>& argv, const std::filesystem::path& cwd,
                         std::string& captured,
                         const std::vector<std::pair<std::string, std::string>>& env = {});

/**
 * @brief  Append-only JSONL event stream.
 *
 * @since      0.1
 * @ingroup    lightroot_exec
 */
class EventLog {
  public:
    /**
     * @brief  Open (or create) an event log at `path`.
     *
     * @param[in] path Log file path (parent directories are created).
     *
     * @throws     None.
     * @note       The file is truncated on open.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_exec
     */
    explicit EventLog(std::filesystem::path path);

    /**
     * @brief  Append one event record as a JSON line.
     *
     * @param[in] package     Package the event concerns.
     * @param[in] phase       Build phase name.
     * @param[in] command     Command run (may be empty).
     * @param[in] status      Outcome status string.
     * @param[in] duration_ms Wall-clock duration in milliseconds.
     * @param[in] log_path    Associated log file (may be empty).
     * @param[in] message     Free-form message (may be empty).
     *
     * @throws     None.
     * @note       No-op if the log is not open.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_exec
     */
    void event(const std::string& package, const std::string& phase, const std::string& command,
               const std::string& status, std::int64_t duration_ms, const std::string& log_path,
               const std::string& message);

    /**
     * @brief  Whether the underlying stream is open.
     *
     * @return     true if the log file is open.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_exec
     */
    [[nodiscard]] bool ok() const { return out_.is_open(); }

  private:
    std::filesystem::path path_; ///< Log file path.
    std::ofstream out_;          ///< Output stream.
};

/**
 * @brief  Wall-clock epoch milliseconds.
 *
 * @return     Milliseconds since epoch (monotonic enough for durations).
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_exec
 */
std::int64_t nowMs();

/**
 * @brief  Read a small file into a string.
 *
 * @param[in] path File to read.
 *
 * @return     File contents, or empty on failure.
 *
 * @throws     None.
 * @note       Best effort; returns empty on any I/O failure.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_exec
 */
std::string readFile(const std::filesystem::path& path);

/**
 * @brief  Number of hardware threads (jobs hint).
 *
 * @return     Online processor count, at least 1.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_exec
 */
int cpuCount();

/**
 * @brief  Search `$PATH` for an executable named @p name.
 *
 * @details Walks each PATH entry, returning the first regular file the current
 *          user may execute. Used to probe host tools (make, tar, fakeroot, ...)
 *          and reused by the doctor checks.
 *
 * @param[in] name Bare tool name, e.g. "fakeroot".
 *
 * @return     Resolved path, or nullopt if not found.
 *
 * @throws     None.
 * @note       None.
 * @warning    None.
 * @since      0.1
 * @ingroup    lightroot_exec
 */
std::optional<std::filesystem::path> findOnPath(std::string_view name);

} // namespace lightroot
