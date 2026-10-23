# The course toolchain image — FIRST WORKING VERSION (Chapter 2).
#
# Chapter 2's lab ends here: a pinned base, the host dependencies,
# the Zephyr SDK and its ARM toolchain, downloaded from the pinned
# release. It builds Zephyr samples. Chapter 3 turns it into the
# production image: the requirements closure, the non-root builder,
# the digest discipline — and deletes nothing you see here, because
# this file's structure was the point.
FROM ubuntu:24.04@sha256:8f9b7b17c30ab84eb0a7e397cc432cd78c819761becafcf86ad9c0a3666fe75f

ENV DEBIAN_FRONTEND=noninteractive

# Zephyr's documented Ubuntu build dependencies.
RUN apt-get update && apt-get install -y --no-install-recommends \
      git cmake ninja-build gperf device-tree-compiler \
      python3 python3-pip python3-venv \
      gcc g++ make file xz-utils \
      wget curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Zephyr SDK 1.0.1: the minimal bundle plus the ARM GNU toolchain —
# the two downloads Chapter 1 warned are several hundred MB.
# Downloaded, extracted and deleted inside ONE RUN, so the tarballs
# never persist in a layer (Chapter 3 measures why that matters).
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

ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-1.0.1 \
    ZEPHYR_TOOLCHAIN_VARIANT=zephyr
WORKDIR /workspace
CMD ["/bin/bash"]
