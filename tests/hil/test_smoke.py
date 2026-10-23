"""The smoke suite — Chapter 10 of Embedded DevOps, Engineered.

Four questions, asked of a real device, in under a minute:

1. Did it boot?                (the banner appears)
2. Is it the image we built?   (the banner carries THIS RUN's
                                stamped version — a stale or wrong
                                image cannot pass by being "a" good
                                image)
3. Does it answer?             (the shell runs Course 1's own
                                `presence` command and the reply is
                                well-formed — sensor path alive)
4. Did anything die on the way? (no fault/assert text anywhere in
                                the boot log)

Smoke, not full test: the deep logic proofs are Chapter 6's unit
tests on native_sim. This suite's job is ground truth — the chip,
the flash, the sensor wiring, the exact signed bytes.
"""

import re

BANNER = r"Starting Zigbee, Engineered - occupancy product, firmware v(\S+)"


def test_device_boots_and_reports_stamped_version(console, expected_version):
    line = console.expect(BANNER, timeout=15)
    seen_version = re.search(BANNER, line).group(1)
    assert seen_version == expected_version, (
        f"device reports v{seen_version}, pipeline stamped "
        f"v{expected_version} — the rig is not running this run's image"
    )


def test_shell_prompt_appears(console):
    console.expect(r"uart:~\$", timeout=15)


def test_presence_command_answers(console):
    console.expect(r"uart:~\$", timeout=15)
    console.send("presence")
    # Any of the three honest answers is a pass — the room may or
    # may not have a person in it. A malformed or missing answer
    # is the failure.
    console.expect(r"presence : (no valid frame received yet|no|YES)",
                   timeout=10)
    console.expect(r"frames\s+: \d+ parsed, \d+ resyncs", timeout=10)


def test_no_faults_in_boot_log(console):
    console.expect(r"uart:~\$", timeout=15)
    faults = [l for l in console.seen
              if re.search(r"ASSERT|FATAL|Hard fault|panic", l)]
    assert not faults, f"fault text in boot log: {faults}"
