# Frontend Plan

Frontends should be layered on top of the same core model.

## Order

Recommended order:

```text
CLI -> TUI -> Qt GUI
```

## CLI

The CLI validates the model and is the base for CI.

Early commands:

```text
lightroot doctor
lightroot list
lightroot resolve
lightroot build
lightroot clean
```

Later commands:

```text
lightroot fetch
lightroot graph
lightroot config get
lightroot config set
lightroot log
```

## TUI

The TUI should replace the painful parts of `menuconfig`.

Important features:

- fast search
- provider selection
- package/category browsing
- conflict explanations
- dependency preview
- build progress
- log preview

Candidate technology: FTXUI.

## Qt GUI

Qt 6 is the planned GUI stack.

The Qt GUI should not be a menuconfig clone. It should make system composition
visible:

- profiles
- selected providers
- package graph
- diagnostics
- configuration diff
- build status
- structured log viewer

Qt should be installed through the host package manager.

The GUI should come after the CLI and TUI prove the core model.
