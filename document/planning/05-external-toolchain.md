# External Toolchain

The first stage should use an external toolchain.

Building GCC/binutils/libc from source is intentionally out of scope for the
initial loop.

## Reasoning

Toolchain builds are slow and full of edge cases. They can delay validation of
the parts LightRoot actually wants to improve first:

- package model
- provider resolver
- build lifecycle
- logs
- CLI/TUI configuration
- rootfs assembly

## First Toolchain Interface

LightRoot should accept a toolchain description rather than guessing everything
from the host.

Possible config:

```toml
[toolchain]
kind = "external"
prefix = "aarch64-linux-gnu-"
path = "/opt/toolchains/gcc-aarch64/bin"
sysroot = "/opt/toolchains/gcc-aarch64/aarch64-linux-gnu/sysroot"
target = "aarch64-linux-gnu"
```

## Doctor Checks

`lightroot doctor` should verify:

- compiler exists
- linker exists
- archiver exists
- strip exists
- sysroot exists
- compiler target matches config
- basic compile test works

## Future Toolchain Work

Later stages may support:

- generated internal toolchains
- imported SDKs
- clang-based cross toolchains
- multiple toolchain profiles
- toolchain lock metadata

These should not block the first rootfs loop.
