FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies for Linux build (GCC, Make, GTK) and Windows cross-build (MinGW)
RUN apt-get update && apt-get install -y \
    build-essential \
    mingw-w64 \
    pkg-config \
    libgtk-3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# Build Linux binaries
RUN make

# Build Windows binaries
RUN make windows

# Package distribution artifacts
RUN mkdir -p dist/windows && \
    cp -r build/windows dist/windows/ && \
    mkdir -p dist/windows/driver && \
    cp src/windows/driver/virtual_com.inf dist/windows/driver/ && \
    mkdir -p dist/windows/installer && \
    cp -r installer dist/windows/installer/ && \
    mkdir -p dist/windows/scripts && \
    cp scripts/install_virtualcom.ps1 dist/windows/scripts/

# Helper script to export artifacts
CMD ["/bin/bash", "-c", "echo 'Artifacts are in /app/dist. Use docker cp to extract them.' && ls -R /app/dist"]
