"""HIL smoke-suite fixtures — Chapter 10 of Embedded DevOps, Engineered.

Two transports, one test suite:

* A real board: pyserial on --port (or $HIL_PORT). The session
  fixture flashes --firmware (the signed .hex the pipeline built —
  a .hex carries its load addresses, so `nrfutil device program`
  can place it; a raw .bin cannot say where it belongs) and then
  interrogates the board. The pipeline passes the exact artifact
  it built, so the suite tests the artifact, not a cousin of it.
* A recorded transcript: --transcript FILE (or $HIL_TRANSCRIPT)
  replays a captured serial session. With --stream, the recording
  is played as ONE continuous, non-rewinding stream — the mode
  that matches live serial. This is how the suite is developed
  and debugged without the rig — and how the course's authoring
  verified the test LOGIC. The physical rig remains a desk check;
  Chapter 10 says so in plain words.

Required mode (--require-hardware): the pipeline's HIL stage runs
this way. Missing --port, --firmware or --expect-version is a
FAILURE, not a skip: a required stage that skips everything and
exits 0 is a green checkmark that tested nothing. Skips exist
only for local, non-required use (transcript debugging, deciding
not to run the version test, and so on).

One stream, one capture: a live serial port cannot rewind, so
the harness never pretends it can. The session fixture captures
the boot log ONCE, into console.boot_seen, and the boot
assertions (banner, prompt, faults) read that capture. Tests
that talk to the shell afterwards drive commands over the same
continuous session. Transcript devices follow the same rule:
there is no reset/rewind, in either mode.

Transcript format: the literal serial text. A line starting with
">>> " marks a command the operator typed; the lines after it,
up to the next ">>>" or EOF, are the device's response. Everything
before the first ">>>" is the boot output.
"""

import os
import re
import subprocess
import time

import pytest

try:
    import serial  # pyserial — the rig's one hardware dependency
except ImportError:  # transcript mode must work without it
    serial = None


def pytest_addoption(parser):
    parser.addoption("--port", default=os.environ.get("HIL_PORT"),
                     help="serial port of the DK (e.g. /dev/ttyACM0)")
    parser.addoption("--transcript", default=os.environ.get("HIL_TRANSCRIPT"),
                     help="recorded serial transcript to replay")
    parser.addoption("--stream", action="store_true",
                     help="replay the transcript as ONE continuous "
                          "non-rewinding stream (live-serial semantics)")
    parser.addoption("--firmware", default=None,
                     help="signed .hex to flash before testing")
    parser.addoption("--expect-version",
                     default=os.environ.get("PRODUCT_VERSION"),
                     help="version the boot banner must report")
    parser.addoption("--require-hardware", action="store_true",
                     help="required mode: missing port/firmware/version "
                          "fails the run instead of skipping tests")


class TranscriptDevice:
    """A serial-shaped replay of a recorded session.

    Deliberately has NO rewind: lines are consumed once, in
    order, exactly like a live port. Commands written by a test
    release their recorded responses, in order, once.
    """

    def __init__(self, path):
        self.boot_lines = []
        self.responses = {}
        current = self.boot_lines
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.rstrip("\n")
                if line.startswith(">>> "):
                    current = self.responses.setdefault(line[4:].strip(), [])
                else:
                    current.append(line)
        self._queue = list(self.boot_lines)
        self._pending = ""

    def write(self, data):
        self._pending += data.decode(errors="replace")
        while "\n" in self._pending:
            line, self._pending = self._pending.split("\n", 1)
            cmd = line.strip().lstrip("\r")
            if cmd in self.responses:
                self._queue.extend(self.responses[cmd])
        return len(data)

    def readline(self):
        if self._queue:
            return (self._queue.pop(0) + "\n").encode()
        return b""


class Console:
    """Line-oriented expect/send over either transport."""

    def __init__(self, dev):
        self.dev = dev
        self.seen = []
        self.boot_seen = []

    def send(self, cmd):
        self.dev.write((cmd + "\n").encode())

    def expect(self, pattern, timeout=10.0):
        """Read lines until one matches the regex; return it.
        Every line read lands in .seen for failure reports."""
        rx = re.compile(pattern)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = self.dev.readline()
            if not raw:
                time.sleep(0.05)
                continue
            line = raw.decode(errors="replace").rstrip()
            self.seen.append(line)
            if rx.search(line):
                return line
        raise AssertionError(
            f"timed out waiting for /{pattern}/; saw:\n"
            + "\n".join(self.seen[-20:])
        )

    def capture_boot(self, timeout=15.0):
        """Read the boot log ONCE, up to the first shell prompt.

        The captured lines are kept in .boot_seen; the boot
        assertions read the capture instead of re-reading a
        stream that has already flowed past.
        """
        self.expect(r"uart:~\$", timeout=timeout)
        self.boot_seen = list(self.seen)
        return self.boot_seen


@pytest.fixture(scope="session")
def device(pytestconfig):
    port = pytestconfig.getoption("--port")
    transcript = pytestconfig.getoption("--transcript")
    firmware = pytestconfig.getoption("--firmware")
    expect_version = pytestconfig.getoption("--expect-version")
    require = pytestconfig.getoption("--require-hardware")

    if require:
        # The pipeline's contract: all three inputs, or the stage
        # fails loudly. (Transcript mode is never required mode:
        # it verifies test logic, not hardware.)
        missing = [name for name, val in
                   (("--port", port), ("--firmware", firmware),
                    ("--expect-version", expect_version)) if not val]
        if missing:
            pytest.fail(
                "required HIL run is missing " + ", ".join(missing) +
                " — refusing to report a green stage that tested nothing",
                pytrace=False)
        if firmware and not firmware.endswith(".hex"):
            pytest.fail(
                f"--firmware must be the signed .hex (it carries its "
                f"load addresses); got {firmware}",
                pytrace=False)

    if port:
        if serial is None:
            if require:
                pytest.fail("pyserial not installed on this rig",
                            pytrace=False)
            pytest.skip("pyserial not installed on this rig")
        if firmware:
            # Flash the artifact under test, then reset into it.
            # nrfutil is Course 1's flashing tool, same family the
            # students already trust.
            subprocess.run(
                ["nrfutil", "device", "program", "--firmware", firmware],
                check=True, capture_output=True, text=True, timeout=120,
            )
        ser = serial.Serial(port, 115200, timeout=0.2)
        if firmware:
            subprocess.run(["nrfutil", "device", "reset"],
                           check=True, capture_output=True, timeout=60)
        yield ser
        ser.close()
    elif transcript:
        dev = TranscriptDevice(transcript)
        yield dev
    else:
        pytest.skip("no --port and no --transcript: nowhere to run")


@pytest.fixture(scope="session")
def console(device):
    """One console for the whole session, over one stream.

    Creating it captures the boot log exactly once; every test
    then works from that capture and/or the live session.
    """
    con = Console(device)
    con.capture_boot()
    return con


@pytest.fixture(scope="session")
def expected_version(pytestconfig):
    v = pytestconfig.getoption("--expect-version")
    if not v:
        if pytestconfig.getoption("--require-hardware"):
            pytest.fail("required HIL run has no --expect-version",
                        pytrace=False)
        pytest.skip("no --expect-version given: version test cannot run")
    return v
