# Positioning

LightRoot is intended to be a more usable Buildroot replacement for an embedded
organization.

The goal is not to create a Yocto-like distribution construction framework, and
not to build a general-purpose package manager. The first target is a small,
practical root filesystem builder with better configuration, clearer logs, and a
more decoupled package/provider model.

## What We Keep From Buildroot

- Simple source-to-rootfs workflow.
- Cross-compilation oriented package lifecycle.
- Direct rootfs/image outputs.
- Lightweight mental model compared with Yocto.
- Ability to build small embedded systems without a full distro stack.

## What We Want To Improve

- `menuconfig` is hard to search, navigate, and reason about.
- Logs are difficult to read when builds fail.
- Package logic is too coupled to Make/Kconfig.
- Swapping components such as BusyBox/Toybox/init/SSL/libc should be explicit.
- Third-party and organization-local packages should be easier to add.
- CI, CLI, TUI, and GUI should share one core model.

## First-Stage Scope

The first working loop should be:

```text
external toolchain -> base userspace package -> rootfs tar
```

The initial packages can focus on BusyBox/Toybox-like base systems.

## Non-Goals For The First Stage

- Building GCC/binutils/libc from source.
- Building Linux kernel and bootloader.
- Producing complete flashable board images.
- Creating a full package ecosystem.
- Building Qt from source.
- Designing the final GUI before CLI/TUI behavior is proven.
