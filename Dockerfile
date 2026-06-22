# flam3fox CLI build/run environment.
#
# The committed autotools files were generated with automake 1.15; on newer
# hosts `make` tries to regenerate them and fails (e.g. aclocal-1.15 missing,
# odd shell errors). Building in this container regenerates them cleanly with
# `autoreconf -fi`, so the CLI builds reproducibly regardless of host.
#
# Build the image (also compiles the CLI into /workspace):
#   docker build -t flam3fox .
#
# Run a render with your own files via a mounted workspace:
#   docker run --rm -v "$PWD:/workspace" flam3fox \
#       bash -lc './flam3-render < test.flam3'
#
# Rebuild after editing sources in the mounted workspace:
#   docker run --rm -v "$PWD:/workspace" flam3fox \
#       bash -lc 'autoreconf -fi && ./configure && make -j"$(nproc)"'
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential autoconf automake libtool pkg-config \
        libxml2-dev libpng-dev libjpeg-dev zlib1g-dev \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# Bake a CLI build into the image for out-of-the-box use. When a workspace is
# mounted over /workspace at run time, rebuild with the command above.
COPY . /workspace
RUN autoreconf -fi \
    && ./configure \
    && make -j"$(nproc)" \
    && echo "flam3-render built:" && ls -l flam3-render

# libflam3 finds its palette here unless $flam3_palettes is set.
RUN mkdir -p /usr/local/share/flam3 \
    && cp -f flam3-palettes.xml /usr/local/share/flam3/ 2>/dev/null || true

CMD ["/bin/bash"]
