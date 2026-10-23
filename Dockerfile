# The course toolchain image — Embedded DevOps, Engineered.
#
# This file IS the build machine, as source code: the exact compiler,
# SDK and Python tools that build the product, frozen into an image
# identified by digest. Chapter 2 builds its first working version;
# Chapter 3 finishes it into this production form; Chapter 6 adds the
# one pinned line at the bottom. Read it top to bottom — every line
# is a decision a chapter explains.
#
# Base image pinned by digest, not tag: a tag can move; a digest is
# the image. (Chapter 3 shows how this digest was captured and how
# to refresh it deliberately.)
FROM ubuntu:24.04@sha256:8f9b7b17c30ab84eb0a7e397cc432cd78c819761becafcf86ad9c0a3666fe75f

ENV DEBIAN_FRONTEND=noninteractive

# Host build dependencies — Zephyr's documented Ubuntu set, plus the
# two Chapter 6 gate tools' system half (cppcheck) — installed from
# the distro so their versions ride the pinned base image.
# Deliberately NOT installed: the Zephyr SDK hosttools bundle —
# apt already provides dtc and gperf (Zephyr's docs allow system
# packages), and Chapter 3 measures what that saves.
RUN apt-get update && apt-get install -y --no-install-recommends \
      git cmake ninja-build gperf device-tree-compiler \
      python3 python3-pip python3-venv \
      gcc g++ make file xz-utils \
      wget curl ca-certificates ccache \
      cppcheck \
    && rm -rf /var/lib/apt/lists/*

# Zephyr SDK 1.0.1 — the minimal bundle (SDK plumbing + cmake glue)
# plus the one GNU toolchain this product needs (arm-zephyr-eabi).
# Downloaded and deleted inside a single RUN: a tarball added in one
# layer and removed in the next still ships in the image; one that
# never survives its own layer does not. Versions are in the URLs —
# there is no "latest" anywhere in this file.
ARG SDK_RELEASE=https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.1
RUN mkdir -p /opt \
 && curl -sL -o /tmp/sdk-min.tar.xz \
      $SDK_RELEASE/zephyr-sdk-1.0.1_linux-x86_64_minimal.tar.xz \
 && tar -xJf /tmp/sdk-min.tar.xz -C /opt \
 && mkdir -p /opt/zephyr-sdk-1.0.1/gnu \
 && curl -sL -o /tmp/sdk-arm.tar.xz \
      $SDK_RELEASE/toolchain_gnu_linux-x86_64_arm-zephyr-eabi.tar.xz \
 && tar -xJf /tmp/sdk-arm.tar.xz -C /opt/zephyr-sdk-1.0.1/gnu \
 && rm /tmp/sdk-min.tar.xz /tmp/sdk-arm.tar.xz

# Python environment: west pinned exactly, then the requirements
# closure copied VERBATIM from the pinned NCS v3.4.1 tree
# (docker/reqs/ in this repo — Chapter 3 shows where each file came
# from). The image's Python set is the workspace's Python set; no
# drift is possible because there is nothing to drift from.
# requirements-run-test.txt is in the list from day one: without it
# Twister dies on a missing natsort module (Chapter 6's story).
COPY docker/reqs/ /tmp/reqs/
RUN python3 -m venv /opt/venv \
 && /opt/venv/bin/pip install --no-cache-dir --upgrade pip \
 && /opt/venv/bin/pip install --no-cache-dir "west==1.5.0" \
      -r /tmp/reqs/requirements-base.txt \
      -r /tmp/reqs/requirements-build.txt \
      -r /tmp/reqs/zephyr-requirements-base.txt \
      -r /tmp/reqs/zephyr-requirements-build-test.txt \
      -r /tmp/reqs/zephyr-requirements-run-test.txt \
      -r /tmp/reqs/mcuboot-requirements.txt \
 && rm -rf /tmp/reqs

# The format gate's tool, pinned on its own line (added in
# Chapter 6): a gate whose checker floats with the package index is
# a random build-breaker, so this one version is named. It rides
# the same venv the requirements installed.
RUN /opt/venv/bin/pip install --no-cache-dir "clang-format==23.1.3"

# A non-root builder whose UID matches a typical host user, so
# files written into bind-mounted workspaces are owned by the
# student, not by root. Ubuntu 24.04's base image already owns
# UID 1000 (the `ubuntu` user) — the -o flag is the fix, and
# Chapter 3's troubleshooting section is this line's biography.
RUN useradd -m -o -u 1000 builder
USER builder
RUN /opt/zephyr-sdk-1.0.1/setup.sh -c >/dev/null 2>&1 || true

ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-1.0.1 \
    ZEPHYR_TOOLCHAIN_VARIANT=zephyr \
    PATH=/opt/venv/bin:$PATH
WORKDIR /workspace
CMD ["/bin/bash"]
