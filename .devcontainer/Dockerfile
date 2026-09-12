# Development image for naemon-core.
#
# The package list mirrors .github/workflows/citest.yml and the distribution
# packages documented in ../.vscode/README.md, so that a build which succeeds
# in here also succeeds in CI.
FROM ubuntu:26.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    # --- naemon-core build dependencies -------------------------------------
    # autogen.sh runs "autoreconf -s -i", which needs automake and libtool on
    # top of autoconf. Both are pre-installed on GitHub runners, which is why
    # citest.yml does not list them.
    build-essential \
    automake \
    autoconf \
    libtool \
    gperf \
    help2man \
    libglib2.0-dev \
    check \
    pkg-config \
    # --- runtime and debugging ----------------------------------------------
    # monitoring-plugins installs into /usr/lib/nagios/plugins, which is the
    # path the "autogen" task passes as --with-pluginsdir.
    gdb \
    monitoring-plugins \
    git \
    curl \
    sudo \
    ca-certificates \
    locales \
    # --- test suite ---------------------------------------------------------
    python3 \
    python3-pip \
    flake8 \
    python3-behave \
    libtest-simple-perl \
    # --- event broker module toolchain --------------------------------------
    # Shared by statusengine/broker (meson) and mod_gearman (autotools).
    cmake \
    meson \
    ninja-build \
    # mod_gearman uses AX_CHECK_COMPILE_FLAG without bundling the macro the way
    # naemon does in acinclude.m4, so its autogen.sh needs the Autoconf Archive.
    autoconf-archive \
    libgearman-dev \
    libssl-dev \
    # mod_gearman only: it links against libltdl.
    libltdl-dev \
    # mod_gearman only: gearman_top links against -lncurses.
    libncurses-dev \
    # statusengine/broker only. Its RabbitMQ backend is switched off via
    # -Drabbitmq=false in the build task, so librabbitmq-dev is not needed.
    libuchardet-dev \
    uuid-dev \
    # --- gearman troubleshooting from inside the container ------------------
    gearman-tools \
    && rm -rf /var/lib/apt/lists/*

# The ubuntu:26.04 image already ships a non-root user "ubuntu" with UID/GID
# 1000. Naemon refuses to start as root (see src/naemon/naemon.c), so the
# dev container has to run as that user.
RUN echo 'ubuntu ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/ubuntu \
    && chmod 0440 /etc/sudoers.d/ubuntu
