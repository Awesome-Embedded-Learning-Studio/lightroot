# Open Questions

These topics are not blockers for writing the first prototype, but they are
worth revisiting.

## Package Manifest Details

- Should dependencies use strings like `virtual:ssl`, or structured tables?
- How should target and host dependency syntax look?
- How should package variants be represented?
- Should patches be ordered by filename or explicitly listed?

## Configuration Model

- Should board, product, and user config be separate files?
- How should profiles override defaults?
- Should generated config be fully expanded or layered?

## Provider Semantics

- Can multiple packages provide the same virtual role at once?
- Are some virtual roles exclusive while others are additive?
- How should LightRoot explain provider conflicts in TUI/GUI?

## Build Execution

- Should LightRoot run commands directly or through generated scripts?
- How much environment isolation is needed in stage one?
- Should package builds be parallel from the beginning?

## Dependency Management For LightRoot Itself

- Should third-party C++ libraries be vendored?
- Should CMake FetchContent be allowed?
- How should offline organization environments consume dependencies?

## Output Layout

- What should the default output directory be?
- Should logs, build dirs, staging, and target rootfs be under one `out/` tree?
- What cleanup levels should exist?

## First Real Target

- Which architecture should prove the first loop?
- Which external toolchain should be the first known-good one?
- Should the first demo use BusyBox or Toybox?
