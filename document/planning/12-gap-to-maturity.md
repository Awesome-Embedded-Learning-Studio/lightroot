# Gap To Maturity

> Snapshot of where LightRoot stands against its stated goal — *a more usable
> Buildroot replacement* — and against mature Buildroot, as of 2026-06-17.
> Drives the sprint roadmap. Source: validated end-to-end build of a real,
> boot-validated aarch64 rootfs (BusyBox 1.36.1 + base-files, booted under
> `qemu-system-aarch64`, 0 panics; OpenSSL builds to target+staging).

## One-line verdict

- Against LightRoot's **own positioning** (usable Buildroot replacement, not a
  clone): the **architecture is ~80% in place and proven**.
- Against **"production tool an org would pick today"**: a few **hard gaps**
  remain (rootfs correctness, image formats, kernel/bootloader).
- Against **mature-Buildroot ecosystem parity** (~3000 packages, 20 years): a
  **years-long breadth gap** — but a full package ecosystem is explicitly a
  first-stage non-goal (see `00-positioning.md`); the "easy to add packages"
  thesis is the lever that amortises that gap.

LightRoot has **already exceeded its stated first-stage scope**
(`external toolchain -> base userspace -> rootfs tar`): it not only produces the
tar, it boots it on cross-compiled aarch64 hardware.

## Area-by-area gaps

| Area | LightRoot now | Mature Buildroot | Gap | Priority for our goal |
|------|---------------|------------------|-----|------------------------|
| Core architecture (resolver, lifecycle, cross-env, event stream, doctor, boot validation) | done, boot-validated | — | ✅ essentially there | — |
| Package breadth | 4 (busybox, openssl, base-files, wget pending) | 3000+ | 🔴 huge (long tail) | medium — amortised by easy-add |
| **Rootfs correctness** (fakeroot, device-table, uid/perms, device nodes) | naive file copy, no fakeroot | fakeroot + device table | 🔴 high | **HIGH — gate to real hardware** |
| Image formats | tar + cpio | ext2/3/4, squashfs, jffs2, ubifs, iso, genimage | 🟡 medium | high (need ≥1 flashable format) |
| Kernel / bootloader | none in-tool (kernel hand-built for validation) | kernel pkg + dtb + U-Boot/GRUB | 🟡 medium (recipe proven reproducible) | high (first-stage non-goal, but production-essential) |
| Backends | script, make-kconfig, make, autotools | +cmake, meson, kconfig-package, python, perl, qmake | 🟡 medium | medium (each ≈ a `defaultPhases` + edges) |
| `patch` phase | lifecycle slot exists, unimplemented | per-package patch series | 🟡 small-medium | medium |
| Download infra (mirrors, fallback, legal-info, lockfile, offline cache) | curl + sha256 | full | 🟡 medium | medium-high (lock + legal are production needs) |
| Config UX (menuconfig replacement) | TOML + CLI | kconfig menuconfig | 🟡 medium (TUI/GUI = Stage 5/6) | **HIGH — core "better than Buildroot" lever** |
| Internal toolchain build | external only (first-stage non-goal) | builds GCC/binutils/libc | 🔴 high | low-medium (explicitly deferred) |
| Reproducible builds / runtime-test framework | manual boot validation | reproducible + qemu runtime tests | 🟡 medium | medium |
| Community / maturity / docs | solo, days | 20+ years, large community | 🔴 not shortcuttable | time dimension |

## The 80/20 to "production-credible for one org's small system"

Ordered by impact (rough solo effort, ranges not precise):

1. **Rootfs correctness** — fakeroot + device-table (correct uid/perms + device
   nodes). This is the gate between "qemu demo" and "goes on real hardware".
2. **1-2 flashable image formats** — squashfs and/or ext4 (via genimage /
   mksquashfs / genext2fs).
3. **`patch` phase** + **lockfile** (reproducible selection pinning).
4. **Kernel package** (recipe already proven reproducible — fold into the tool).
5. Curate **30-50 core packages** for that org's stack + cmake/meson backend.

≈ **2-4 focused months** to "usable by a specific org".

To "broadly competitive usable alternative" (+ TUI, + mirror/legal, + a few
hundred packages, + board presets, + reproducible builds) ≈ **6-12 months**.
Full ecosystem parity ≈ **years** — but largely unnecessary if the easy-add
thesis holds (orgs self-serve packages).

## The two most honest points

- The **breadth chasm is real and is a long tail** (wget's autotools
  cross-dependency hell just demonstrated it). But if "adding a package" stays
  cheap (TOML manifest + lifecycle), orgs add their own and we never need to
  chase all 3000.
- The **real production blocker is NOT architecture — it is rootfs correctness +
  image format + kernel package.** Close those and LightRoot moves from "runs a
  demo" to "ships a real system".

## Current sprint target

Sprint 1 of this maturity push: **rootfs correctness** — fakeroot-wrapped
assembly + a device-table (device nodes, uid/perms, standard dirs) so the
produced rootfs is correct enough for real hardware, then a flashable image
format. This is the single biggest lever on "demo vs production".
