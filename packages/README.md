# Package Trees

LightRoot indexes package trees as overlays. Roots are scanned in order; a
later root overrides an earlier one with the same `(scope, name)` and the
override is reported as a duplicate for diagnostics (see
[document/planning/03-package-model.md](../document/planning/03-package-model.md)
and §"Package Overlays").

- `upstream/` — upstream/public package manifests.
- `company/` — organization-local overrides and additional packages.

Add more roots on the CLI with `lightroot list --root <path>`.

Each manifest is one TOML file:

```toml
[package]
name = "busybox"
version = "1.36.1"
scope = "target"            # or "host"
provides = ["virtual:base-utils"]
depends = ["target:openssl >= 3.0", "virtual:ssl", "host:ninja"]

[source]
kind = "archive"
url = "..."
sha256 = "..."

[build]
backend = "make-kconfig"    # or cmake, make, autotools, meson, script
```
