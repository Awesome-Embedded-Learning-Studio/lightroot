# Architecture

LightRoot should be split into small, testable layers.

```text
CLI / TUI / Qt GUI
        |
        v
LightRoot Core
  - config model
  - package registry
  - provider resolver
  - build plan
  - diagnostics
        |
        v
Executor
  - fetch
  - verify
  - extract
  - patch
  - configure
  - build
  - install
  - rootfs assembly
        |
        v
Outputs
  - rootfs
  - tar/images
  - logs
  - build plan
  - lock file
```

## Core Rule

Frontends should not own build logic.

CLI, TUI, and GUI should call the same core APIs:

- load config
- load package registry
- resolve providers and dependencies
- produce diagnostics
- produce build plans
- read structured build events

This avoids three frontends growing three different interpretations of the same
system.

## CMake Role

CMake is an invoke layer, not the brain of LightRoot.

Expected flow:

```text
cmake --build build --target lightroot-rootfs
        |
        v
lightroot build --config ...
```

CMake should help IDEs, CI, and C++ developers enter the system naturally. It
should not encode package dependency resolution or provider selection.

## Package Backend Rule

LightRoot should not force every third-party package to become a CMake package.

Packages may use native build systems:

- CMake
- Make
- Make/Kconfig
- Autotools
- Meson
- custom scripts

LightRoot owns the lifecycle and environment; the backend owns package-specific
build commands.
