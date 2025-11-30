#!/bin/bash
# OpenPLC OPC UA Installation Script
# This script installs open62541 library for OPC UA support

echo "Installing open62541 OPC UA library..."

# Check if open62541 is already installed
if pkg-config --exists open62541; then
    echo "open62541 is already installed"
    exit 0
fi

# Install dependencies
echo "Installing dependencies..."
sudo apt-get update
sudo apt-get install -y build-essential cmake git pkg-config

# Clone and build open62541
echo "Cloning open62541 repository..."
cd /tmp
git clone https://github.com/open62541/open62541.git
cd open62541

echo "Building open62541..."
mkdir build && cd build
cmake -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
sudo make install
sudo ldconfig

echo "open62541 installation completed!"
echo "You can now compile OpenPLC with OPC UA support."
