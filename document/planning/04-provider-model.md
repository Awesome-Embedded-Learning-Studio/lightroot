# Provider Model

The provider model is a core LightRoot feature.

It should allow the system to express roles such as "init system" or "SSL
library" without hard-coding one package everywhere.

## Rule Of Thumb

A virtual capability should represent a system role or interchangeable
implementation, not every normal package feature.

## Good Virtual Candidates

```text
virtual:libc
virtual:base-utils
virtual:shell
virtual:init
virtual:ssl
virtual:logger
virtual:dev-manager
virtual:net-manager
virtual:resolver
virtual:dhcp-client
virtual:ssh-server
```

## Examples

```text
virtual:base-utils
  -> busybox
  -> toybox
  -> coreutils

virtual:init
  -> busybox-init
  -> systemd
  -> s6
  -> openrc
  -> custom-init

virtual:dev-manager
  -> busybox-mdev
  -> eudev
  -> systemd-udevd
  -> custom-devd

virtual:net-manager
  -> busybox-ifupdown
  -> ifupdown-ng
  -> connman
  -> networkmanager
  -> systemd-networkd
  -> custom-netd

virtual:ssl
  -> openssl
  -> mbedtls
  -> wolfssl
```

## Network And Device Management

Network and device management are not build architecture features by themselves.

They are still good virtual capabilities because a final embedded system often
needs one primary implementation for each role.

LightRoot should not need to understand all runtime details. It should only know
which package provides the selected role and how that affects dependencies,
conflicts, and rootfs installation.

## Profiles

Profiles can select groups of providers.

Example:

```text
profile:minimal
  virtual:base-utils = busybox
  virtual:shell = busybox-ash
  virtual:init = busybox-init
  virtual:dev-manager = busybox-mdev
  virtual:net-manager = busybox-ifupdown
```

This can make TUI/GUI configuration much easier than a raw option list.
