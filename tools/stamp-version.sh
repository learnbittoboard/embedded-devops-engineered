#!/usr/bin/env bash
# stamp-version.sh — derive the product version from git and write
# it into app/VERSION. Chapter 7 of Embedded DevOps, Engineered.
#
# The rule (the whole chapter in one function): THE TAG IS THE
# TRUTH. Nobody edits a version number by hand before a release,
# because the manual bump is where release mistakes are born.
#
#   HEAD is exactly tag vX.Y.Z   ->  version X.Y.Z, tweak 0
#   N commits past tag vX.Y.Z    ->  version X.Y.(Z+1), tweak N,
#                                    display X.Y.(Z+1)-dev.N
#                                    (a dev build: its X.Y.Z sorts
#                                    AFTER the release it follows
#                                    and is not overtaken by the
#                                    release it precedes — that
#                                    release carries the same X.Y.Z
#                                    with tweak reset to 0, and
#                                    MCUboot's default comparison
#                                    reads X.Y.Z only, so the
#                                    sequence never goes backwards)
#   no release tags yet           ->  version 0.1.0, tweak = commit count
#
# Only tags shaped like a release (v<digit>…) count. The --match
# below is load-bearing, not decoration: this repo also carries
# other tags (the course's own chapter tags, experiment tags), and
# git describe happily returns the NEAREST tag of any shape. Feed
# it "ch11" and the version arithmetic below stops being funny.
#
# THE CANONICAL DISPLAY VERSION is the string this script prints:
# X.Y.Z at a tag, X.Y.Z-dev.N between tags. Every witness must
# carry exactly this string — the boot banner, the release
# manifest, the pipeline's PRODUCT_VERSION, and the HIL suite's
# --expect-version. They agree because they are all stamped from
# this one output:
#
#   * app/VERSION's EXTRAVERSION carries the display suffix:
#     Zephyr composes APP_VERSION_STRING as X.Y.Z when
#     EXTRAVERSION is empty and X.Y.Z-<EXTRAVERSION> when it is
#     not — so EXTRAVERSION = dev.N makes the banner print the
#     display version verbatim. (Leaving EXTRAVERSION empty was
#     the old bug: the banner printed plain X.Y.Z while every
#     other witness said X.Y.Z-dev.N, and the HIL version check
#     could never pass on a dev build.)
#   * The imgtool sign stamp stays the separate NUMERIC form
#     X.Y.Z+T: that is what MCUboot and the OTA header compare,
#     and it is never displayed to a human as "the version".
#
# The version is stamped into EVERY input the build reads —
# and this product reads two, on purpose. Course 1 split them
# deliberately: app/VERSION feeds the app's own version (the
# boot banner), while prj.conf's CONFIG_MCUBOOT_IMGTOOL_SIGN_
# VERSION stamps the signed image header AND composes the
# .zigbee OTA file version. Two knobs that must agree, Course 1
# said — and left agreeing them to human discipline. This
# script is where that discipline becomes machinery: one git
# truth, written into both knobs, every run. (Writing only
# VERSION is the classic half-stamp: the banner moves, the
# header doesn't, and the pipeline ships the disagreement.)
#
# Prints the display version on stdout (the pipeline captures it);
# diagnostics go to stderr.
set -euo pipefail
cd "$(dirname "$0")/.."

# The pipeline runs this inside a container whose user may not own
# the checkout; git would rather complain than help. This directory
# is ours — say so. (Chapter 7's troubleshooting box.)
git config --global --add safe.directory "$PWD" 2>/dev/null || true

if desc=$(git describe --tags --long --match 'v[0-9]*' 2>/dev/null); then
	# v1.3.0-5-gdeadbee -> tag=v1.3.0, ahead=5
	tag=${desc%-*-g*}
	ahead=${desc#"$tag"-}
	ahead=${ahead%%-*}
	base=${tag#v}
	major=${base%%.*}
	rest=${base#*.}
	minor=${rest%%.*}
	patch=${rest#*.}
	if [ "$ahead" -eq 0 ]; then
		display="$major.$minor.$patch"
		extra=""
	else
		patch=$((patch + 1))
		display="$major.$minor.$patch-dev.$ahead"
		extra="dev.$ahead"
	fi
	tweak=$ahead
else
	major=0; minor=1; patch=0
	tweak=$(git rev-list --count HEAD)
	display="$major.$minor.$patch-dev.$tweak"
	extra="dev.$tweak"
	echo "stamp-version: no git tag found — pre-release versioning" >&2
fi

cat > app/VERSION <<EOF
VERSION_MAJOR = $major
VERSION_MINOR = $minor
PATCHLEVEL = $patch
VERSION_TWEAK = $tweak
EXTRAVERSION = $extra
EOF

# The second knob: the imgtool sign stamp in prj.conf. Its value
# is "X.Y.Z+T" — the header's +build number is the tweak, and
# the OTA file version packs the same four numbers into its
# four bytes (1.3.1+4 -> 0x01030104). If the knob is not where
# the product keeps it, fail loudly: a stamp that silently
# missed its target is worse than no stamp at all.
sign_version="$major.$minor.$patch+$tweak"
if ! grep -q '^CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION=' app/prj.conf; then
	echo "stamp-version: CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION not found in app/prj.conf — the product's version knob moved; refusing to half-stamp" >&2
	exit 1
fi
sed -i 's|^CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION=.*|CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION="'"$sign_version"'"|' app/prj.conf

echo "stamp-version: $display (tweak $tweak) -> app/VERSION + prj.conf sign stamp $sign_version" >&2
echo "$display"
