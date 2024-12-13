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

#Building 
meson build/
ninja -C build/
sudo ninja -C build/ install
