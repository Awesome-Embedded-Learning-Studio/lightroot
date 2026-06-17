# Roadmap

This roadmap favors a small working loop over a large incomplete platform.

## Stage 0: Design Lockdown

- Write planning documents.
- Agree on C++/CMake/Qt direction.
- Define first rootfs target.
- Decide first package manifest shape.
- Decide first external toolchain config shape.

## Stage 1: Core Resolver

- Create C++ project skeleton.
- Parse project config TOML.
- Parse package manifests.
- Index package registry.
- Resolve package dependencies.
- Resolve virtual providers.
- Emit build plan JSON.
- Add tests for provider conflicts and dependency cycles.

## Stage 2: Doctor And Toolchain

- Implement `lightroot doctor`.
- Validate external toolchain paths.
- Validate compiler target.
- Run a small compile test.
- Report host dependency status.

## Stage 3: Executor Skeleton

- Implement package lifecycle phases.
- Add per-phase logs.
- Emit JSONL event stream.
- Run simple script/make packages.
- Produce staging and target rootfs directories.

## Stage 4: First Rootfs Tar

- Build or install a BusyBox/Toybox-like base package.
- Assemble minimal rootfs.
- Generate rootfs tar.
- Keep logs readable on success and failure.

## Stage 5: TUI

- Add TUI package browser.
- Add search.
- Add provider selection.
- Add diagnostics display.
- Add build progress/log view.

## Stage 6: Qt GUI

- Build Qt GUI on the same core model.
- Display profiles, providers, packages, diagnostics, and logs.
- Keep Qt optional for headless builds.

## Later

- Offline source cache.
- Mirror support.
- Lock file.
- Board definitions.
- Kernel and bootloader support.
- Internal toolchain builder.
- More package backends.
