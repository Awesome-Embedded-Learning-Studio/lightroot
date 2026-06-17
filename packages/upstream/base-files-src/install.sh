#!/bin/sh
# Install the minimal rootfs skeleton into $LIGHTROOT_DESTDIR.
set -e
D="$LIGHTROOT_DESTDIR"
for d in dev proc sys tmp root run var etc/init.d; do
    mkdir -p "$D/$d"
done
cp -a etc/. "$D/etc/"
cp init "$D/init"
chmod 0755 "$D/init"
