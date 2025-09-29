#!/bin/bash
# Build script for Linux/WSL

# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build . -j4

# Run example
if [ -f examples/socket_example ]; then
    echo "Running socket example..."
    ./examples/socket_example
fi