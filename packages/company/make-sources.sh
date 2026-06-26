#!/usr/bin/env bash
#
# Generate clean source tarballs from imx-forge's git submodules for the lightroot
# i.MX packages. `git archive` excludes .git and any build artifacts, so each
# tarball is a pristine source snapshot. The archived ref is the one imx-forge's
# own release builders reset to before patching — i.e. the exact base the patches
# apply against — NOT the submodule working tree's current (possibly patched) HEAD.
#
# Output: packages/company/archives/<component>-<shortsha>.tar.xz (+ prints sha256
# for pasting into the manifest). Re-run when the imx-forge submodules move.
#
# Usage: ./packages/company/make-sources.sh [component ...]
#   components default to: uboot-imx linux-imx
set -euo pipefail

IMX_FORGE="${IMX_FORGE:-$HOME/imx-forge}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${SCRIPT_DIR}/archives"
mkdir -p "$OUT_DIR"

# Component -> (submodule path under third_party/, git ref to archive).
declare -A REF=(
    ["uboot-imx"]="origin/lf_v2025.04"
    ["linux-imx"]="origin/lf-6.12.y"
)

archive_one() {
    local comp="$1"
    local sub="third_party/${comp}"
    local src="${IMX_FORGE}/${sub}"
    local ref="${REF[$comp]:-HEAD}"

    if [ ! -d "${src}" ]; then
        echo "skip: ${src} not found" >&2
        return 0
    fi
    if ! git -C "${src}" rev-parse --verify "${ref}" >/dev/null 2>&1; then
        echo "error: ${comp}: ref '${ref}' missing (run: git -C ${src} fetch origin)" >&2
        return 1
    fi

    local sha; sha=$(git -C "${src}" rev-parse --short "${ref}")
    local tb="${OUT_DIR}/${comp}-${sha}.tar.gz"
    # --prefix gives a single top-level dir so lightroot's --strip-components=1
    # lands the source at the workdir root. git archive has no tar.xz, so tar.gz
    # (lightroot's `tar -xf` auto-detects the compression).
    git -C "${src}" archive --format=tar.gz --prefix="${comp}/" "${ref}" -o "${tb}"
    echo "${comp}  ref=${ref}  sha=${sha}"
    echo "  file=${tb}"
    printf "  sha256=%s\n" "$(sha256sum "${tb}" | awk '{print $1}')"
}

 comps=("$@")
if [ ${#comps[@]} -eq 0 ]; then
    comps=("uboot-imx" "linux-imx")
fi
for c in "${comps[@]}"; do
    archive_one "$c"
done
