# LightRoot

A more usable Buildroot-style embedded rootfs builder. Planning lives in
[document/planning/](document/planning/); the locked design decisions are in
[document/planning/11-design-decisions.md](document/planning/11-design-decisions.md).

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Dependencies (toml++, CLI11, fmt, nlohmann/json, GoogleTest) are fetched via
CPM.cmake on first configure. For offline builds, set `CPM_SOURCE_CACHE` to a
shared cache directory.

## Test

```sh
ctest --test-dir build --output-on-failure
```

## Run

```sh
./build/src/cli/lightroot list            # index and list packages
./build/src/cli/lightroot list --root packages/upstream
./build/src/cli/lightroot resolve         # (Stage 1 core resolver: pending)
./build/src/cli/lightroot doctor          # (Stage 2)
```

## Status

Stage 1 in progress — core typed IR, project config + package manifest
parsing, and the package registry. Dependency/provider resolution, build-plan
emission, the executor, and the first BusyBox rootfs loop are later stages
(see [document/planning/09-roadmap.md](document/planning/09-roadmap.md)).
