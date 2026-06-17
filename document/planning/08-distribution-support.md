# Distribution Support

The first supported host distributions should be Ubuntu and Arch Linux.

This matches the current familiarity and keeps the support matrix small.

## Initial Host Targets

- Ubuntu LTS
- Arch Linux

## Initial Requirements

Likely requirements:

```text
CMake >= 3.20
C++20 compiler
Ninja recommended
Git
tar
xz
zstd eventually
external cross toolchain
Qt 6 only for GUI builds
```

## Ubuntu Direction

Expected system packages:

```text
cmake
ninja-build
g++
git
qt6-base-dev
```

Qt is only required when building the GUI.

## Arch Direction

Expected system packages:

```text
cmake
ninja
gcc
git
qt6-base
```

Qt is only required when building the GUI.

## Not First-Stage Targets

- Full Windows support.
- macOS rootfs builds.
- Building Qt from source.
- Supporting every Linux distribution from day one.

Other distributions can be added once the dependency and doctor model is stable.
