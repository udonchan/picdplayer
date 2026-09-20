FROM debian:trixie

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        pkg-config \
        libasound2-dev \
        libdiscid-dev \
        libcurl4-openssl-dev \
        nlohmann-json3-dev \
        libwebsockets-dev \
        file \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
