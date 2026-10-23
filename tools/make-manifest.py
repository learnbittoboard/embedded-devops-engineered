#!/usr/bin/env python3
"""make-manifest.py — write the manifest.json a pipeline run archives
next to its artifacts. Chapter 7 of Embedded DevOps, Engineered.

A folder of binaries says "here are some files". A manifest says
WHAT they are: which product, which version, which commit, built
for which board, inside which toolchain image, and — by checksum —
that the bytes you are holding are the bytes the pipeline made.
Chapter 8 completes it with signature verification; Chapter 9
publishes it in the release.

Usage:
    make-manifest.py --version 1.3.0 --board nrf52840dk/nrf52840 \
        --toolchain-image toolchain:1.0.0-ncs3.4.1@sha256:... \
        --out build/manifest.json FILE [FILE...]
"""

import argparse
import hashlib
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


def git(*args, default=""):
    try:
        return subprocess.run(
            ["git", *args], capture_output=True, text=True, check=True
        ).stdout.strip()
    except Exception:
        return default


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--product", default="occupancy-sensor")
    ap.add_argument("--version", required=True)
    ap.add_argument("--board", required=True)
    ap.add_argument("--toolchain-image", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()

    artifacts = []
    for name in a.files:
        p = Path(name)
        if not p.is_file():
            print(f"make-manifest: missing artifact: {name}", file=sys.stderr)
            return 1
        artifacts.append(
            {"path": name, "size": p.stat().st_size, "sha256": sha256_of(p)}
        )

    manifest = {
        "product": a.product,
        "version": a.version,
        "git_sha": git("rev-parse", "HEAD"),
        "git_describe": git("describe", "--tags", "--long", default="no-tags"),
        "board": a.board,
        "toolchain_image": a.toolchain_image,
        "built_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "artifacts": artifacts,
    }

    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"make-manifest: wrote {out} ({len(artifacts)} artifacts)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
