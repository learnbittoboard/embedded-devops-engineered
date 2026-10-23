"""Version-contract tests — Chapter 7 of Embedded DevOps, Engineered.

Run on any host with git + python (no Zephyr needed):

    python3 -m pytest tests/test_stamp_version.py

The contract under test (ONE canonical display version):

* tools/stamp-version.sh prints the display version: X.Y.Z at a
  release tag, X.Y.Z-dev.N N commits past it.
* The same script writes that exact string into the inputs the
  firmware reads: app/VERSION carries it as X.Y.Z plus
  EXTRAVERSION (= dev.N between tags, empty at a tag), which is
  precisely how Zephyr composes APP_VERSION_STRING — the boot
  banner. Banner == display, by construction, not by hope.
* The imgtool sign stamp stays the separate numeric X.Y.Z+T form.
* OTA ordering: MCUboot's default version comparison
  (boot_compare_version, without MCUBOOT_VERSION_CMP_USE_BUILD_
  NUMBER) reads major/minor/revision only. Over a dev -> release
  cycle the compared X.Y.Z sequence must never decrease:
  1.3.0 (release) -> 1.3.1 (dev builds) -> 1.3.1 (release) ->
  1.3.2 (next dev builds). Non-decreasing, end to end.
"""

import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def run(cmd, cwd):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                          check=True).stdout.strip()


def make_scratch(tmp_path):
    """A minimal product-shaped git repo: the stamp script, a
    VERSION file, and the prj.conf knob it rewrites."""
    scratch = tmp_path / "product"
    (scratch / "tools").mkdir(parents=True)
    (scratch / "app").mkdir()
    (scratch / "tools" / "stamp-version.sh").write_bytes(
        (REPO / "tools" / "stamp-version.sh").read_bytes())
    (scratch / "app" / "VERSION").write_text(
        "VERSION_MAJOR = 0\nVERSION_MINOR = 0\nPATCHLEVEL = 0\n"
        "VERSION_TWEAK = 0\nEXTRAVERSION =\n")
    (scratch / "app" / "prj.conf").write_text(
        'CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION="0.0.0+0"\n')
    run(["git", "init", "-q", "-b", "main"], scratch)
    run(["git", "config", "user.email", "test@example.com"], scratch)
    run(["git", "config", "user.name", "Test"], scratch)
    run(["git", "add", "-A"], scratch)
    run(["git", "commit", "-qm", "base"], scratch)
    return scratch


def stamp(scratch):
    display = run(["bash", "tools/stamp-version.sh"], scratch)
    version_file = (scratch / "app" / "VERSION").read_text()
    prj = (scratch / "app" / "prj.conf").read_text()
    fields = dict(re.findall(r"^(\w+) = (.*)$", version_file, re.M))
    sign = re.search(r'SIGN_VERSION="([^"]+)"', prj).group(1)
    return display, fields, sign


def banner_string(fields):
    """APP_VERSION_STRING as Zephyr's version.cmake composes it:
    X.Y.Z, plus -<EXTRAVERSION> when EXTRAVERSION is non-empty."""
    base = (f"{fields['VERSION_MAJOR']}.{fields['VERSION_MINOR']}."
            f"{fields['PATCHLEVEL']}")
    extra = fields.get("EXTRAVERSION", "")
    return f"{base}-{extra}" if extra else base


def test_display_banner_manifest_all_agree_at_tag_and_dev(tmp_path):
    scratch = make_scratch(tmp_path)
    run(["git", "tag", "v1.3.0"], scratch)

    display, fields, sign = stamp(scratch)
    assert display == "1.3.0"
    assert banner_string(fields) == display, "banner != display at tag"
    assert sign == "1.3.0+0"

    run(["git", "commit", "-q", "--allow-empty", "-m", "one"], scratch)
    run(["git", "commit", "-q", "--allow-empty", "-m", "two"], scratch)

    display, fields, sign = stamp(scratch)
    assert display == "1.3.1-dev.2"
    assert banner_string(fields) == display, (
        "banner != display on a dev build — the original defect")
    assert fields["EXTRAVERSION"] == "dev.2"
    assert sign == "1.3.1+2"


def xyz_of(sign_stamp):
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)\+(\d+)", sign_stamp)
    assert m, sign_stamp
    return tuple(int(g) for g in m.groups()[:3])


def test_dev_to_release_ota_ordering_never_decreases():
    # The worked example from Chapter 7, as sign stamps:
    sequence = [
        "1.3.0+0",   # release v1.3.0
        "1.3.1+1",   # dev, 1 commit past
        "1.3.1+2",   # dev, 2 commits past
        "1.3.1+0",   # release v1.3.1 (tweak resets at the tag)
        "1.3.2+1",   # next dev cycle
    ]
    compared = [xyz_of(s) for s in sequence]  # MCUboot default: X.Y.Z
    assert compared == sorted(compared), (
        f"X.Y.Z sequence decreases somewhere: {compared}")
    # And the dev builds really do sort after the release they
    # follow and are not overtaken by the release they precede:
    assert xyz_of("1.3.1+2") > xyz_of("1.3.0+0")
    assert xyz_of("1.3.1+0") >= xyz_of("1.3.1+2")
