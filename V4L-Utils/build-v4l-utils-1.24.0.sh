#!/bin/bash

# default variable
INSTALL_DIR="/home/yongqi/Vscode_workspace/V_Project1/V4L-Utils/v4l-utils/install_dir"
BUILD_DIR="/home/yongqi/Vscode_workspace/V_Project1/V4L-Utils/v4l-utils/build_dir"

# process command variable
while getopts "b:i:" opt; do
  case $opt in
    b) BUILD_DIR="$OPTARG" ;;
    i) INSTALL_DIR="$OPTARG" ;;
    *) echo "Usage: $0 [-b build_dir] [-i install_dir]" >&2
       exit 1 ;;
  esac
done

# set cross-compilation toolchain path
export TOOLCHAIN_PATH="/home/yongqi/Desktop/arm64_resource/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin"
# export PATH="$TOOLCHAIN_PATH:$PATH"

export CC="$TOOLCHAIN_PATH/aarch64-none-linux-gnu-gcc"
export CXX="$TOOLCHAIN_PATH/aarch64-none-linux-gnu-g++"
export AR="$TOOLCHAIN_PATH/aarch64-none-linux-gnu-ar"
export LD="$TOOLCHAIN_PATH/aarch64-none-linux-gnu-ld"
export RANLIB="$TOOLCHAIN_PATH/aarch64-none-linux-gnu-ranlib"
export STRIP="$TOOLCHAIN_PATH/aarch64-none-linux-gnu-strip"

# set sysroot path
export SYSROOT="/home/yongqi/Desktop/arm64_resource/system"
# set compiler flags
export CFLAGS="-I$SYSROOT/usr/include"
export CXXFLAGS="-I$SYSROOT/usr/include"
export LDFLAGS="-L$SYSROOT/usr/lib"

# enter v4l-utils directory
cd /home/yongqi/Vscode_workspace/V_Project1/V4L-Utils/v4l-utils || { echo "Failed to enter v4l-utils directory"; exit 1; }

# cleanup previous build
#make clean || { echo "Failed to clean previous build"; exit 1; }

# run bootstrap.sh if it exists
if [ -f "bootstrap.sh" ]; then
    ./bootstrap.sh || { echo "Failed to run bootstrap.sh"; exit 1; }
fi

# create build directory
mkdir -p "$BUILD_DIR" || { echo "Failed to create build directory"; exit 1; }
cd "$BUILD_DIR" || { echo "Failed to enter build directory"; exit 1; }

# configure
../configure \
    --host=aarch64-none-linux-gnu \
    --prefix="$INSTALL_DIR" \
    --disable-qv4l2 \
    --disable-qvidcap \
    --disable-v4l2gl \
    || { echo "Configure failed"; exit 1; }

# compile
make -j$(nproc) || { echo "Build failed"; exit 1; }

# install
sudo make install || { echo "Install failed"; exit 1; }

echo "v4l-utils has been successfully built and installed to $INSTALL_DIR"
