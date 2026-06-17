/**
 * @file    unique_fd.h
 * @author  Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief   Move-only RAII wrapper for a POSIX file descriptor.
 * @version 0.1
 * @date    2026-06-16
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * UniqueFd owns a raw POSIX file descriptor and closes it with ::close() on
 * destruction (including early-return and exception paths). It is move-only,
 * so a descriptor is closed exactly once. Use it to replace manual ::close()
 * bookkeeping around ::open()/::pipe()/::accept().
 *
 * @ingroup lightroot_base
 */
#pragma once

#include <unistd.h>

namespace lightroot {

/**
 * @brief  Exclusive owner of a POSIX file descriptor.
 *
 * @details Closes the held descriptor in its destructor. Move transfers
 *          ownership and leaves the source empty; copy is disabled.
 *
 * @note       An empty UniqueFd (default-constructed or moved-from) holds -1
 *             and is a no-op on destruction.
 * @warning    Do not ::close() the descriptor obtained from get()/release()
 *             while the UniqueFd still owns it — release() first.
 * @since      0.1
 * @ingroup    lightroot_base
 *
 * @code
 * UniqueFd fd(::open(path, O_RDONLY));
 * if (!fd) return;
 * // ...use fd.get()... closed automatically on every return path.
 * @endcode
 */
class UniqueFd {
  public:
    /**
     * @brief  Constructs a guard owning @p fd (default -1 = empty).
     *
     * @param[in] fd Descriptor to own, or -1 for an empty guard.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_base
     */
    explicit UniqueFd(int fd = -1) noexcept : fd_(fd) {}

    /**
     * @brief  Closes the owned descriptor, if any.
     *
     * @throws     None (close errors are ignored).
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_base
     */
    ~UniqueFd() noexcept { reset(); }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    /**
     * @brief  Move constructor: takes ownership, empties @p other.
     *
     * @param[in,out] other Guard to steal the descriptor from.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_base
     */
    UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

    /**
     * @brief  Move assignment: closes current, takes @p other's descriptor.
     *
     * @param[in,out] other Guard to steal the descriptor from.
     *
     * @return     Reference to this guard.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_base
     */
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(other.fd_);
            other.fd_ = -1;
        }
        return *this;
    }

    /**
     * @brief  Returns the owned descriptor (-1 when empty).
     *
     * @return     The raw descriptor; ownership is NOT released.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_base
     */
    int get() const noexcept { return fd_; }

    /**
     * @brief  Releases ownership without closing.
     *
     * @return     The raw descriptor, now caller-owned.
     *
     * @throws     None.
     * @note       After release the guard is empty.
     * @warning    The caller becomes responsible for closing the descriptor.
     * @since      0.1
     * @ingroup    lightroot_base
     */
    int release() noexcept {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

    /**
     * @brief  Closes the current descriptor (if any) and adopts @p fd.
     *
     * @param[in] fd New descriptor to own (-1 leaves the guard empty).
     *
     * @throws     None (close errors are ignored).
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_base
     */
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0)
            ::close(fd_);
        fd_ = fd;
    }

    /**
     * @brief  Checks whether the guard owns a valid descriptor.
     *
     * @return     true if get() >= 0, false otherwise.
     *
     * @throws     None.
     * @note       None.
     * @warning    None.
     * @since      0.1
     * @ingroup    lightroot_base
     */
    explicit operator bool() const noexcept { return fd_ >= 0; }

  private:
    /// Owned descriptor, or -1 when empty. Ownership: owner.
    int fd_;
};

} // namespace lightroot
