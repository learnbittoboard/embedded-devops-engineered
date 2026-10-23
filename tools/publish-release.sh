#!/usr/bin/env bash
# publish-release.sh — publish an assembled bundle as a GitHub
# Release. Chapter 9 of Embedded DevOps, Engineered.
#
#   tools/publish-release.sh RELEASE_DIR
#
# Runs AFTER the approval gate (the Jenkinsfile's input step): by
# the time this script executes, a human has already said "ship
# it". Requires: the GitHub CLI (gh) authenticated — in Jenkins,
# GH_TOKEN arrives from the github-token credential; on a desk,
# `gh auth login`. The tag is read from the bundle's manifest, so
# what publishes is what the manifest says was built.
set -euo pipefail

dir=${1:?usage: publish-release.sh RELEASE_DIR}

version=$(python3 -c "import json;print(json.load(open('$dir/manifest.json'))['version'])")
tag="v$version"
prev=$(git describe --tags --abbrev=0 "$tag^" 2>/dev/null || echo "")

# Release notes from the commit range. They are only as good as
# the commit messages — Chapter 9's discipline lesson — so the
# generator stays dumb and the team stays honest.
notes=$(mktemp)
{
	echo "## Firmware $tag"
	echo
	if [ -n "$prev" ]; then
		echo "Changes since $prev:"
		echo
		git log --no-merges --pretty='- %s' "$prev..$tag"
	else
		echo "Initial release."
		echo
		git log --no-merges --pretty='- %s' "$tag"
	fi
	echo
	echo "Artifacts: signed image (.bin/.hex), DFU package (.zip),"
	echo "Zigbee OTA package (.zigbee), manifest.json, checksums.txt."
} > "$notes"

if gh release view "$tag" >/dev/null 2>&1; then
	# The withdrawal drill's other half: re-publishing a version
	# replaces its assets with THIS bundle's — promotion and
	# rollback use the same door.
	gh release upload "$tag" "$dir"/* --clobber
	gh release edit "$tag" --notes-file "$notes"
	echo "publish-release: $tag already existed — assets replaced"
else
	gh release create "$tag" "$dir"/* --title "Firmware $tag" \
		--notes-file "$notes"
	echo "publish-release: $tag published"
fi
rm -f "$notes"
