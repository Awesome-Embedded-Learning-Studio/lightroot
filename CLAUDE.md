# CLAUDE.md — Claude Code Entry Point

> Project conventions live in **AGENT.md** — the single source of truth shared with
> Codex CLI. It is imported below. This file adds **only** Claude-specific notes;
> do not duplicate conventions here.

@AGENT.md

## Claude-specific

- **Workflow output language**: Chinese (technical terms remain in English).
- **Code comments**: English, per the Doxygen spec in AGENT.md.
- **Commit messages**: English.
- **Tooling**: `.clang-format` + `.clang-tidy` are present and config-only.
  Prefer `clang-format -i` over hand-formatting; trust clangd's clang-tidy hints
  but they do not gate builds.
- **Style quick reference**: PascalCase types, `camelCase` functions, trailing-`_`
  members, `UPPER_CASE` constants, `lower_case` params/locals. See AGENT.md
  "Code Style" for the full table.
