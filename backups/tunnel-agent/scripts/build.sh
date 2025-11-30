#!/bin/bash

# Build script for Protogate Tunnel Agent

set -e

echo "Building Protogate Tunnel Agent..."

# Setup vcpkg if needed
if [ ! -d "vcpkg" ]; then
    echo "Installing vcpkg..."
    git clone https://github.com/Microsoft/vcpkg.git
    ./vcpkg/bootstrap-vcpkg.sh
fi

# Install dependencies
echo "Installing dependencies..."
./vcpkg/vcpkg install

# Configure
echo "Configuring CMake..."
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake

# Build
echo "Building..."
cmake --build build --config Release -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

echo "Build complete! Binary: ./build/tunnel-agent"
