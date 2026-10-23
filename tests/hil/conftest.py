"""HIL smoke-suite fixtures — Chapter 10 of Embedded DevOps, Engineered.

Two transports, one test suite:

* A real board: pyserial on --port (or $HIL_PORT). The session
  fixture optionally flashes --firmware with nrfutil first — the
  pipeline passes the exact signed image it built, so the suite
  interrogates the artifact, not a cousin of it.
* A recorded transcript: --transcript FILE (or $HIL_TRANSCRIPT)
  replays a captured serial session through the same read/write
  interface. This is how the suite is developed and debugged
  without the rig — and how the course's authoring verified the
  test LOGIC. The physical rig remains a desk check; Chapter 10
  says so in plain words.

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
    parser.addoption("--firmware", default=None,
                     help="signed .hex to flash before testing")
    parser.addoption("--expect-version",
                     default=os.environ.get("PRODUCT_VERSION"),
                     help="version the boot banner must report")


class TranscriptDevice:
    """A serial-shaped replay of a recorded session."""

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

    def reset(self):
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


@pytest.fixture(scope="session")
def device(pytestconfig):
    port = pytestconfig.getoption("--port")
    transcript = pytestconfig.getoption("--transcript")
    firmware = pytestconfig.getoption("--firmware")

    if port:
        if serial is None:
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


@pytest.fixture()
def console(device):
    if isinstance(device, TranscriptDevice):
        device.reset()
    return Console(device)


@pytest.fixture(scope="session")
def expected_version(pytestconfig):
    v = pytestconfig.getoption("--expect-version")
    if not v:
        pytest.skip("no --expect-version given: version test cannot run")
    return v
