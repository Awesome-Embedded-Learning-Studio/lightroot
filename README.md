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
./build/src/cli/lightroot resolve         # resolve deps + providers into a build plan
./build/src/cli/lightroot doctor          # check host + toolchain readiness
./build/src/cli/lightroot build           # build the resolved plan into a rootfs image
```

### i.MX6ULL build (U-Boot + Linux + rootfs + SD image)

`lightroot-imx.toml` builds the i.MX6ULL artifacts from the sibling `imx-forge`
source tree with the Arm GNU toolchain (`arm-none-linux-gnueabihf-`):

```sh
# 1. Generate source tarballs from imx-forge's git submodules (uboot-imx, linux-imx).
./packages/company/make-sources.sh

# 2. Doctor-check the Arm toolchain, then build U-Boot + Linux + BusyBox rootfs
#    and assemble the bootable SD image at out-imx/imx6ull-aes-sd.img.
./build/src/cli/lightroot doctor -c lightroot-imx.toml
./build/src/cli/lightroot build  -c lightroot-imx.toml
```

The i.MX packages live under `packages/company/` (the overlay root scanned
alongside `packages/upstream/`). The flashable SD image is described by an
`[image]` table in `lightroot-imx.toml` (payload offset, partitions, boot files)
— lightroot's image renderer is board-agnostic, so any board expresses its layout
as data with no C++ changes.

## Status

Stage 1 in progress — core typed IR, project config + package manifest
parsing, and the package registry. Dependency/provider resolution, build-plan
emission, the executor, and the first BusyBox rootfs loop are later stages
(see [document/planning/09-roadmap.md](document/planning/09-roadmap.md)).
