# Investigation #221 only; not a production runtime or the default build image.
FROM debian:trixie
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config curl ca-certificates xz-utils \
    python3 ruby perl bison flex gperf unifdef \
    libglib2.0-dev libharfbuzz-dev libicu-dev libjpeg-dev libepoxy-dev \
    libgcrypt20-dev libsoup-3.0-dev libtasn1-6-dev libxkbcommon-dev \
    libxml2-dev libxslt1-dev libpng-dev libsqlite3-dev libwebp-dev zlib1g-dev \
    libfreetype-dev libfontconfig-dev libdrm-dev libgbm-dev libegl-dev \
    libgles-dev libinput-dev libudev-dev libsystemd-dev \
    libatk1.0-dev libatk-bridge2.0-dev libavif-dev libjxl-dev liblcms2-dev \
    libwoff-dev libseccomp-dev bubblewrap xdg-dbus-proxy \
    && rm -rf /var/lib/apt/lists/*
RUN curl --fail --location --retry 3 --connect-timeout 15 --max-time 300 \
    https://wpewebkit.org/releases/wpewebkit-2.54.0.tar.xz -o /tmp/wpewebkit.tar.xz \
    && printf '%s  %s\n' \
       efa9bcc3cb891c2d88f50eec710d9ccee71cbdf1040420361eb98c17355eb452 \
       /tmp/wpewebkit.tar.xz | sha256sum --check --strict \
    && mkdir -p /opt/wpe-source \
    && tar -xf /tmp/wpewebkit.tar.xz --strip-components=1 -C /opt/wpe-source \
    && rm /tmp/wpewebkit.tar.xz
# 2.54.0 compiles this media binding even with ENABLE_VIDEO=OFF (#221).
COPY scripts/wpe-source-video-disabled.patch /tmp/video-disabled.patch
RUN patch --batch --fuzz=0 -d /opt/wpe-source -p1 < /tmp/video-disabled.patch
COPY scripts/wpe-source-sandbox-link.patch /tmp/sandbox-link.patch
RUN patch --batch --fuzz=0 -d /opt/wpe-source -p1 < /tmp/sandbox-link.patch
COPY scripts/wpe-source-probe.sh /usr/local/bin/wpe-source-probe
WORKDIR /work
ENTRYPOINT ["bash", "/usr/local/bin/wpe-source-probe"]
