#!/bin/bash

#switch to the v4l-utils submodule directory
if ! cd submodules/v4l-utils/; then

    echo "cannot change directory to v4l-utils"

    exit 1

fi

# pull the specific tag
TAG="v4l-utils-1.28.1"
if ! git fetch --tags; then

    echo "can not fetch tags"

    exit 1

fi
if ! git checkout "$TAG"; then

    echo "can not checkout tag $TAG"

    exit 1

fi
#install the dependencies
sudo apt install debhelper doxygen gcc git graphviz libasound2-dev libjpeg-dev libqt5opengl5-dev libudev-dev libx11-dev meson pkg-config qtbase5-dev udev libsdl2-dev libbpf-dev llvm clang

sudo apt install alsa-lib-devel doxygen libjpeg-turbo-devel qt5-qtbase-devel libudev-devel mesa-libGLU-devel

sudo apt install libjson-c-dev

#Building 
meson build/
ninja -C build/
meson configure -Dgconv=enabled build/
