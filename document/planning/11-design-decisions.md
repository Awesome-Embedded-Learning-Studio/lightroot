# Design Decisions

This document closes out Stage 0 by committing to answers for the open
questions raised in [10-open-questions.md](10-open-questions.md). These are the
decisions that gate writing the first code.

## 1. Dependency Acquisition — CPM.cmake

Use [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake), not hand-written
`FetchContent` blocks and not a system-package dependency model.

CPM is a thin wrapper over `FetchContent` that removes the per-package
`FetchContent_Declare` boilerplate:

```cmake
CPMAddPackage("gh:marzer/tomlplusplus#v3.4.0")
```

It also answers the offline concern directly:

- `CPM_SOURCE_CACHE` (env var or CMake var) points all builds at one shared
  cache. An organization can mirror or rsync that directory so air-gapped
  machines consume already-fetched sources.
- `SOURCE_DIR` lets a fully vendored source tree be plugged in for the most
  locked-down environments.
- Because CPM sits on `FetchContent`, switching between cache and vendored
  never breaks the build.

Layout: `cmake/cpm_bootstrap.cmake` (single-file download/include) plus
`cmake/dependencies.cmake` (all version pins in one place). `lightroot doctor`
gains a check that the CPM cache / vendored sources are present.

## 2. Dependency Syntax — String Surface, Typed IR, Real Parser

Strings on the TOML surface are fine. Robustness does **not** come from making
the manifest use verbose structured tables; it comes from three layers:

1. **Surface (TOML, for humans):** keep string specs, they are ergonomic and
   familiar (Cargo, npm, conan all use string specs).

   ```toml
   depends = ["target:openssl >= 3.0", "virtual:ssl", "host:ninja"]
   ```

2. **Boundary (parser):** a real recursive-descent parser turns a string into a
   typed struct and reports line/column on rejection. No ad-hoc
   `string::find(':')`.

   ```cpp
   struct DependencyRef {
     enum class Scope { Host, Target };
     enum class Kind  { Package, Virtual };
     Scope   scope;
     Kind    kind;
     std::string name;
     VersionConstraint version;   // >= 3.0
     std::vector<std::string> features;
   };
   ```

3. **Core (resolver):** operates only on `DependencyRef`. Raw strings are
   banned past the parse boundary.

Scaling needs — version constraints, host/target distinction, features/variants
— all grow out of this model without breaking it. Rare complex dependencies may
use a table form as an escape hatch, but parse into the same `DependencyRef`.

Grammar:

```text
spec := [ scope ":" ] ( "virtual:" ident | ident ) [ cmp version ] [ features ]
scope := "host" | "target"
cmp   := ">=" | "<=" | "==" | "!=" | ">" | "<" | "="
features := "[" ident ("," ident)* "]"
```

## 3. First Target — BusyBox on aarch64

- Architecture: `aarch64-linux-gnu`.
- Toolchain: external, the host-distribution cross compiler
  (`gcc-aarch64-linux-gnu` on Ubuntu, `aarch64-linux-gnu-gcc` on Arch). Known
  good, no self-build.
- Base package: BusyBox 1.36.1, `make-kconfig` backend.

First rootfs loop:

```text
doctor (verify gcc/ar/strip, sysroot, compile smoke)
  -> fetch busybox -> verify sha256 -> extract
  -> defconfig + config fragment overlay
  -> cross compile (CONFIG_STATIC=y)
  -> install into staging/target
  -> assemble minimal rootfs (busybox + symlinks + dir tree)
  -> rootfs.tar
```

Notes:

- **Static BusyBox first.** No libc copy in the first loop; rootfs is minimal
  and hardest to get wrong. Dynamic + libc staging is a follow-up.
- **Config via `defconfig` + fragments**, not one giant `.config`. This is the
  first concrete improvement over `menuconfig` — config deltas are visible and
  diffable.
- **Validation without a board:** run the assembled rootfs under
  `qemu-aarch64` (user or system mode) to prove the userspace works.

## 4. Provider Semantics

Answers to the three provider questions in
[10-open-questions.md](10-open-questions.md):

### Cardinality

Each virtual capability carries a cardinality.

```cpp
enum class ProviderCardinality { Exclusive, Additive };
```

First-stage virtuals (`libc`, `base-utils`, `init`, `ssl`, `shell`,
`dev-manager`, `net-manager`) are all **Exclusive** — a system has one libc,
one init. `Additive` (e.g. `virtual:firmware`, where multiple blobs coexist) is
modeled but not exercised until a real need appears.

### Selection Rule — Explicit Over Implicit

The resolver must never silently guess. For each required capability:

1. An explicit selection wins and is traceable:
   `[providers] virtual:ssl = "openssl"` from config / board / profile.
2. If exactly one package in the dependency closure provides it, use it.
3. Zero providers → diagnostic: `virtual:ssl required but no package provides
   it`.
4. Multiple candidates and no explicit selection → unresolved state, surfaced
   as a diagnostic / interactive TUI choice. Never a silent default.
5. Exclusive capability with two selected providers → conflict diagnostic.

Conflict diagnostics show who requires the capability, who provides it, what is
currently selected and where that choice came from, and how to fix it:

```text
virtual:ssl required by: target:curl, target:wget
providers: openssl, mbedtls
currently selected: openssl (from profile:minimal)
conflict: mbedtls also selected via boards/x/config.toml
fix: [providers] virtual:ssl = "openssl"
```

### Profiles

A profile pre-selects a group of providers so common combinations resolve with
zero manual choices (see the `profile:minimal` example in
[04-provider-model.md](04-provider-model.md)).

### Provided Versions

The IR keeps a `satisfies` field so a package can provide a capability at a
version (e.g. `openssl` provides `virtual:ssl` satisfying `>= 3.0`). Not
enforced in the first stage; kept as a forward-compatibility hook.
