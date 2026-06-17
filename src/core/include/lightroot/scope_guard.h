/**
 * @file    scope_guard.h
 * @author  Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief   RAII scope guard for automatic cleanup.
 * @version 0.1
 * @date    2026-06-16
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Provides ScopeGuard, a move/copy-disabled RAII type that runs a stored
 * callable on destruction unless dismissed. Ported from the CFDesktop base
 * utilities so error-path cleanup (closing fds, rolling back partial work)
 * cannot be skipped by an early return.
 *
 * @ingroup lightroot_base
 */
#pragma once

#include <functional>
#include <utility>

namespace lightroot {

/**
 * @brief  RAII scope guard that runs a cleanup callable on destruction.
 *
 * @details Executes the supplied callable when the guard goes out of scope,
 *          unless dismiss() was called first. Use it to guarantee cleanup
 *          (close a descriptor, restore state) on every exit path, including
 *          early returns.
 *
 * @note       Not movable and not copyable, so cleanup runs exactly once.
 * @warning    The stored callable runs during destruction; an exception it
 *             throws propagates during stack unwinding.
 * @since      0.1
 * @ingroup    lightroot_base
 *
 * @code
 * int fd = ::open(path, O_RDONLY);
 * if (fd < 0) return;
 * ScopeGuard close_fd([&] { ::close(fd); });
 * // ...use fd... closed automatically on every return path.
 * @endcode
 */
class ScopeGuard {
  public:
    /**
     * @brief  Constructs a guard bound to a cleanup callable.
     *
     * @param[in] f Callable invoked with no arguments on destruction.
     *
     * @throws     Only if copying/moving @p f throws.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_base
     */
    template <typename F> explicit ScopeGuard(F&& f) : fn_(std::forward<F>(f)) {}

    /**
     * @brief  Runs the cleanup callable unless the guard was dismissed.
     *
     * @throws     Propagates any exception thrown by the stored callable.
     * @note       None.
     * @warning    Exceptions thrown by the callable propagate during stack
     *             unwinding.
     * @since      0.1
     * @ingroup    lightroot_base
     */
    ~ScopeGuard() noexcept(false) {
        if (active_)
            fn_();
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard&&) = delete;
    ScopeGuard& operator=(ScopeGuard&&) = delete;

    /**
     * @brief  Releases the guard without running the cleanup callable.
     *
     * @throws     None.
     * @note       Irreversible: once dismissed the callable will not run.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_base
     */
    void dismiss() noexcept { active_ = false; }

  private:
    /// Cleanup callable executed on destruction. Ownership: owner.
    std::function<void()> fn_;
    /// Whether the callable still fires on destruction. Ownership: owner.
    bool active_ = true;
};

} // namespace lightroot
