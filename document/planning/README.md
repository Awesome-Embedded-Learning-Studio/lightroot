# LightRoot Planning

This directory records early design decisions for LightRoot.

LightRoot is planned as a more usable Buildroot-style system builder for an
embedded organization. The project should keep Buildroot's useful simplicity,
but improve configuration UX, package decoupling, provider selection, logs, and
observability.

These documents are intentionally planning notes, not final specs.

## Documents

- [00-positioning.md](00-positioning.md): project scope and non-goals
- [01-architecture.md](01-architecture.md): high-level components
- [02-technology-stack.md](02-technology-stack.md): C++, CMake, TUI, Qt decisions
- [03-package-model.md](03-package-model.md): package manifest direction
- [04-provider-model.md](04-provider-model.md): virtual capability design
- [05-external-toolchain.md](05-external-toolchain.md): first-stage toolchain strategy
- [06-logging-and-diagnostics.md](06-logging-and-diagnostics.md): structured logs
- [07-frontend-plan.md](07-frontend-plan.md): CLI, TUI, Qt GUI path
- [08-distribution-support.md](08-distribution-support.md): host distro decisions
- [09-roadmap.md](09-roadmap.md): staged implementation plan
- [10-open-questions.md](10-open-questions.md): topics still worth discussing
