# The course Jenkins image — Chapter 4 of Embedded DevOps, Engineered.
#
# Stock Jenkins LTS, pinned exactly (version AND digest — the same
# pinning rule as the toolchain image), plus the course's plugin set
# installed at image build time by the official plugin CLI. Plugins
# are part of the factory's definition, so they are built into the
# image from a file in this repo — never clicked in by hand.
FROM jenkins/jenkins:2.580.1-lts-jdk21@sha256:9a3b7cbb24287325b444dc93c8aadbce461ebca156fd486acc0f3ee9c8bbc5b3

# The Docker CLI, copied from Docker's official CLI image (a small
# multi-stage build — Chapter 3's pattern, used for real): the
# controller shells out to `docker` to run build containers, and the
# stock Jenkins image does not carry the CLI. The socket it talks to
# is mounted by docker-compose.yml.
COPY --from=docker:27.5.1-cli /usr/local/bin/docker /usr/local/bin/docker

# plugins.txt names the five plugins the course teaches (the intent);
# plugins-frozen.txt is the full 70-plugin dependency closure with
# exact versions, resolved once and frozen (the guarantee). The image
# installs the frozen file: what Chapter 4 tested is what every
# student gets, bit for bit.
COPY plugins-frozen.txt /usr/share/jenkins/ref/plugins.txt
RUN jenkins-plugin-cli --plugin-file /usr/share/jenkins/ref/plugins.txt
