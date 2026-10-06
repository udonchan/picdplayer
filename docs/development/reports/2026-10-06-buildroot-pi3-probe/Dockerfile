FROM debian:trixie-slim@sha256:a99cfc517144bc59b1978475ec53b46ecabec7e43635402ee5b77cc54cd1b20a

RUN apt-get update && apt-get install -y --no-install-recommends \
    bc bison build-essential ca-certificates cpio curl file flex git \
    libncurses-dev make patch perl python3 rsync tar unzip wget xz-utils \
    && rm -rf /var/lib/apt/lists/*

RUN useradd --uid 1000 --create-home builder
USER builder
WORKDIR /work
