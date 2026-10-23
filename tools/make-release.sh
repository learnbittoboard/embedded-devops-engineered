#!/usr/bin/env bash
# make-release.sh — assemble the release bundle from a pipeline
# build directory. Chapter 9 of Embedded DevOps, Engineered.
#
#   tools/make-release.sh BUILD_DIR RELEASE_DIR
#
# A release is a PROMOTION, not a rebuild: the exact artifacts the
# pipeline built, signed, verified and archived — copied, never
# regenerated — plus the manifest that names them and a checksums
# file so anyone can prove the bytes they downloaded are the bytes
# the pipeline made. If any expected asset is missing, this script
# fails: a partial release is worse than no release.
set -euo pipefail

build=${1:?usage: make-release.sh BUILD_DIR RELEASE_DIR}
out=${2:?usage: make-release.sh BUILD_DIR RELEASE_DIR}

mkdir -p "$out"

copy() {
	# copy <glob...> — fails if a pattern matches nothing.
	local found=0
	for f in $1; do
		[ -e "$f" ] || continue
		cp "$f" "$out/"
		found=1
	done
	[ "$found" -eq 1 ] || { echo "make-release: MISSING: $1" >&2; exit 1; }
}

copy "$build/app/zephyr/zephyr.signed.bin"
copy "$build/app/zephyr/zephyr.signed.hex"
copy "$build/app/zephyr/zephyr.elf"
copy "$build/dfu_application.zip"
copy "$build/*.zigbee"
copy "$build/manifest.json"

(cd "$out" && sha256sum -- * > checksums.txt)

echo "make-release: bundle ready in $out:"
ls -la "$out"
