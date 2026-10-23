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

# Output semantics, deliberate: RELEASE_DIR belongs to this
# script. It is CLEANED at the start of every run, so a bundle
# contains exactly this run's assets — never a stale file from a
# previous bundle, and never a previous checksums.txt waiting to
# be hashed into its own successor (the old script's second run
# did exactly that, and verification then failed on a bundle
# that had never left the building). The guard refuses the two
# arguments nobody means.
[ -n "$out" ] && [ "$out" != "/" ] || {
	echo "make-release: refusing RELEASE_DIR '$out'" >&2; exit 1; }
mkdir -p "$out"
rm -f "$out"/*

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

# checksums.txt is written LAST, into the cleaned directory, and
# names only the bundle's assets — on any run, first or fiftieth.
(cd "$out" && sha256sum -- * > checksums.txt)

echo "make-release: bundle ready in $out:"
ls -la "$out"
