# Package Model

Packages describe source metadata, dependency metadata, provided capabilities,
and build lifecycle behavior.

The package model should learn from Buildroot's lifecycle without copying its
Make/Kconfig expression model.

## Expected Lifecycle

```text
fetch -> verify -> extract -> patch -> configure -> build -> install-staging -> install-target
```

Not every package needs every phase, but the executor should think in these
terms.

## Manifest Direction

TOML is the preferred manifest format.

Possible shape:

```toml
[package]
name = "busybox"
version = "1.36.1"
summary = "Small Unix utilities in a single binary."
license = "GPL-2.0-only"
provides = ["virtual:base-utils", "virtual:shell", "virtual:init"]
depends = []

[source]
kind = "archive"
url = "https://busybox.net/downloads/busybox-1.36.1.tar.bz2"
sha256 = "..."

[build]
backend = "make-kconfig"
```

## Host And Target Packages

LightRoot must distinguish tools built for the host from packages installed into
the target rootfs.

Examples:

```text
host:pkgconf
host:cmake
target:busybox
target:openssl
```

This should be represented explicitly in dependencies and build plans.

## Package Overlays

Organization-specific package trees should be supported.

Possible roots:

```text
packages/upstream
packages/company
boards/product-a/packages
```

Overlay priority must be deterministic, and duplicate package definitions should
produce useful diagnostics.

## Third-Party Package Goal

Adding an internal package should feel like writing metadata and lifecycle
commands, not editing a central monolithic build file.
