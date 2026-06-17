# AGENT.md — lightroot Project Conventions for AI Agents

> **Single source of truth** for project conventions, shared across all AI agents
> (Claude Code, OpenAI Codex CLI). Tool-specific entry points (e.g. `CLAUDE.md`,
> `AGENTS.md`) reference this file and add only tool-specific notes. **Edit
> conventions here; do not duplicate them** in the entry points.

## Project Overview

- **Name**: lightroot — embedded rootfs builder (manifest-driven package resolver)
- **Tech**: C++20 / CMake 3.20+ / Ninja / CPM.cmake
- **Dependencies** (via CPM, all pinned): `fmt#10.2.1`, `CLI11#v2.4.2`,
  `tomlplusplus#v3.4.0`, `nlohmann/json#v3.11.3`, `GTest#v1.14.0`
- **Layout**: `src/core` (static library `lightroot_core`) + `src/cli` (executable
  `lightroot`) + `tests` (`test_lightroot`)

## Build System

| Action | Command |
|--------|---------|
| Configure | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug` |
| Build | `cmake --build build` |
| Test | `ctest --test-dir build --output-on-failure` |
| Run CLI | `./build/src/cli/lightroot <list\|resolve\|doctor> [opts]` |
| Format | `find src tests -type f \( -name '*.h' -o -name '*.cpp' \) -print0 \| xargs -0 clang-format -i` |

Build options: `LIGHTROOT_BUILD_CLI` (default ON), `LIGHTROOT_BUILD_TESTS`
(default ON).

## Code Style

Style is aligned with the author's CFDesktop codebase. Hard rules:

- **Formatter**: LLVM `.clang-format`, 100-char width, 4-space indent, attached
  braces, pointer/reference left-aligned, access modifiers flush-left.
- **Types/structs/classes/enums**: `PascalCase`. Enum values: `PascalCase`.
- **Methods / free functions**: `camelCase` (domain), e.g. `parseManifest`,
  `resolveDep`, `findTarget`. Single-word methods stay as-is (`resolve`, `find`,
  `add`, `ok`). **std-mirroring names stay `snake_case`** and carry a
  `// NOLINTNEXTLINE`: `to_string`, `to_json`.
- **Parameters / local variables / globals**: `lower_case` (`snake_case`).
- **Member variables**: `lower_case` with a **trailing underscore** (`reg_`,
  `out_`, `path_`). Do **not** use an `m_` prefix.
- **Constants** (constexpr / global / static): `UPPER_CASE` (`VIRTUAL_PREFIX`,
  `HOST_PREFIX`).
- **Namespace**: `lightroot` (flat; no nested namespaces).
- **Files**: `snake_case`, extension `.h` / `.cpp`.
- **Header guard**: `#pragma once`.
- **Includes**: own headers `<lightroot/foo.h>` (angle, via the include dir),
  sorted case-insensitively by clang-format.

Naming is encoded in `.clang-tidy` (`readability-identifier-naming`) and is
**config-only** — clangd surfaces violations as in-editor hints; it is **not** run
in CI. The clang-tidy rules match the code (trailing-underscore members, lower_case
params/locals, camelCase functions), unlike a generic LLVM preset.

## Coding Taste

Preferences distilled from the codebase — write new code to match. Not lint-enforced.

- **RAII (preferred strongly)**: wrap OS resources in owning types. `ScopeGuard`
  (`scope_guard.h`, ported from CFDesktop) runs cleanup on scope exit;
  `UniqueFd` (`unique_fd.h`) owns a POSIX fd and closes it on every return path.
  Never leave a raw `::close` on an error path when a wrapper can do it.
- **Error handling**: structured `Diagnostic`s from the resolver. Failable
  single-value operations return `lightroot::expected<T, E>` (`expected.h`,
  ported from CFDesktop's `cf::expected` — a C++17 `std::expected` mirror), e.g.
  `parseDependency` → `expected<DependencyRef, ParseError>` and
  `parseVersion`/`parseVersionConstraint` → `expected<T, std::string>` (leaf
  parsers carry a reason string as the error). Multi-error parsers
  (manifest/config) still return `std::optional<T>` + an out-param
  `std::vector<Error>`. No exceptions for control flow.
- **Modern C++**: `auto` for local deduction, `constexpr` for compile-time
  constants, `std::string_view` for non-owning params, `[[nodiscard]]` on
  non-void public functions, move semantics where it saves a copy.
- **Ownership**: prefer values and `const&`; raw pointers are non-owning
  observers only. No manual `new`/`delete`.
- **Public API**: declared in `src/core/include/lightroot/*.h`; implementations
  in `src/core/src/*.cpp`. File-local helpers live in an anonymous `namespace {}`
  inside the `.cpp`.

## Module Map

### src/core → lightroot_core
- `types.h` — `Scope`, `DepKind`, `ProviderCardinality` + `to_string`.
- `version.h` — `Version`, `Cmp`, `VersionConstraint`, parsers.
- `dependency.h` — `DependencyRef`, `ParseError`, `parseDependency`.
- `manifest.h` — `Package`, `SourceInfo`, `BuildInfo`, manifest parsers.
- `config.h` — `ProjectConfig`, `ToolchainConfig`, project config parsers.
- `registry.h` — `PackageRegistry` (overlay index).
- `resolver.h` — `Resolver`, `BuildStep`, `BuildPlan`, `to_json`.
- `diagnostic.h` — `Diagnostic`, `DiagSeverity`.
- `exec.h` — `runCommand`/`runCapture`, `EventLog`, `nowMs`/`readFile`/`cpuCount`.
- `scope_guard.h`, `unique_fd.h` — RAII primitives (header-only, `lightroot::`).
- `expected.h` — `lightroot::expected<T,E>` (C++17 `std::expected` mirror, ported from CFDesktop).

### src/cli → lightroot
- `main.cpp` — CLI11 subcommands `list` / `resolve` / `doctor`; wires the core
  library end to end.

## Doxygen Conventions

Every public header and public symbol carries a Doxygen block. Rules:

- **File header**: `@file`, `@author`, `@brief`, `@version`, `@date`,
  `@copyright`, `@details`, `@ingroup`.
- **Per public symbol** (class/struct/enum/free function/public method): `@brief`,
  optional `@details`, `@param[in|out|in,out]`, `@return`, `@throws` (or `None`),
  `@note` (or `None`), `@warning` (or `None`), `@since`, `@ingroup`.
- Use `/** */` blocks; `///<` for trailing member docs.
- Third-person present tense; comment lines ≤ 100 chars. English only.

## Testing Conventions

- **Framework**: GoogleTest (`gtest_main`).
- **Pattern**: `tests/test_<module>.cpp`; helper functions `camelCase`
  (`makePkg`, `hasDiag`, `inPlan`).
- **Fixtures**: `tests/data/*.toml` copied next to the test binary by the
  `lightroot_test_data` custom target.
