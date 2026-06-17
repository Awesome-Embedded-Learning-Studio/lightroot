# Logging And Diagnostics

Readable logs are a major LightRoot goal.

Buildroot-style stdout floods should be avoided. The terminal should show a
clear summary, while detailed logs remain available per package and per phase.

## Log Layout

Possible output layout:

```text
out/logs/events.jsonl
out/logs/summary.txt
out/logs/packages/busybox/fetch.log
out/logs/packages/busybox/configure.log
out/logs/packages/busybox/build.log
out/logs/packages/busybox/install-target.log
```

## Event Stream

The executor should emit structured events.

Example event fields:

```text
timestamp
level
package
phase
command
status
duration
log_path
message
```

CLI, TUI, and GUI should read the same event stream.

## Failure Output

On failure, LightRoot should show:

```text
package: busybox
phase: build
command: make -j16
log: out/logs/packages/busybox/build.log
last useful lines:
  ...
```

The key is to show the failure context immediately without hiding the full log.

## Diagnostics Style

Diagnostics should explain cause and next action.

Examples:

```text
virtual:ssl has multiple providers: openssl, mbedtls, wolfssl
choose one in [providers]
```

```text
toolchain compiler not found: /opt/toolchains/foo/bin/aarch64-linux-gnu-gcc
check [toolchain].path or install the external toolchain
```
