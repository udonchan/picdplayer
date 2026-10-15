FROM debian:trixie

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        pkg-config \
        libasound2-dev \
        libdiscid-dev \
        libcurl4-openssl-dev \
        libcdio-paranoia-dev \
        nlohmann-json3-dev \
        libwebsockets-dev \
        file \
        nodejs \
        python3 \
        libglib2.0-dev libsoup-3.0-dev libgbm-dev libdrm-dev \
        libxkbcommon-dev libegl-dev libgles-dev \
        libepoxy0 libatk1.0-0t64 libatk-bridge2.0-0t64 libseccomp2 \
        libinput10 libudev1 libxkbcommon0 libsystemd0 libicu76 libgbm1 libdrm2 \
        libxml2 libsqlite3-0 libxslt1.1 liblcms2-2 libgcrypt20 libwebpdemux2 \
        libjxl0.11 libavif16 libharfbuzz-icu0 libjpeg62-turbo libpng16-16t64 \
        libharfbuzz0b libfreetype6 libfontconfig1 libwebpmux3 libwebp7 \
        libexpat1 libtasn1-6 bubblewrap xdg-dbus-proxy \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
