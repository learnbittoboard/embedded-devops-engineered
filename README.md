# Occupancy Sensor — Product Repo (Embedded DevOps, Engineered)

The finished Course 1 product — the Zigbee occupancy-sensor firmware for the
nRF52840 DK (HLK-LD2410 mmWave sensor, MCUboot-signed images, `.zigbee` OTA
packages) — plus the delivery factory this course builds around it:

| Path | What it is | Chapter |
|---|---|---|
| `app/` | The firmware (sysbuild app: app + MCUboot images) | Course 1 |
| `Dockerfile` | The pinned toolchain image definition | 2–3 |
| `docker-compose.yml` | Jenkins, stood up as code | 4 |
| `Jenkinsfile` | The pipeline: build → gates → sign → release | 5–9 |
| `app/tests/` | Host unit tests (ztest/Twister) for the frame parser | 6 |
| `tools/` | Manifest generator, release helpers | 7, 9 |
| `tests/hil/` | Hardware-in-the-loop smoke suite (pytest + pyserial) | 10 |
| `.github/workflows/` | The same pipeline in GitHub Actions | 11 |

The git history is the course: one commit per chapter, tagged `ch01`…`ch11`.
Check out any tag to see the repo exactly as that chapter leaves it.
