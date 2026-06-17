# Technology Stack

The preferred implementation language is modern C++.

## Core Stack

- Language: C++20 first, C++23 only when clearly useful.
- Build system: CMake.
- Config/manifest format: TOML.
- CLI: native C++.
- TUI: C++ TUI library, likely FTXUI.
- GUI: Qt 6, installed from the host distribution.
- CMake integration: thin invoke wrapper.

## Candidate Libraries

Initial candidates:

- `toml++` for TOML parsing.
- `CLI11` for command line parsing.
- `fmt` for formatting.
- `spdlog` or a small custom layer over `fmt` for human logs.
- `nlohmann/json` or another lightweight JSON library for build plans/events.
- `FTXUI` for TUI.
- Qt 6 for GUI.

The project should be careful with dependencies. Header-only or mature CMake
friendly libraries are preferred early.

## Python Position

Python should not be a required LightRoot runtime dependency.

Python can still appear as:

- a temporary prototype tool
- an optional package hook dependency
- a helper script dependency for specific packages

If a package requires Python, `lightroot doctor` should report that as a package
or build dependency, not as a core LightRoot requirement.

## Qt Position

Qt is the chosen GUI technology.

Qt should come from the host distribution package manager. LightRoot should not
build Qt itself in the normal path.

The GUI should come after the CLI and TUI validate the core model.
