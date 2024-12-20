#!/bin/bash

# setting the default variable value
TAG_DEFAULT="v4l-utils-1.28.1"
INSTALL_PATH_DEFAULT="/home/yongqi/Vscode_workspace/V_Project1/submodules/v4l-utils/install_dir"
BUILD_PATH_DEFAULT="build/"
ACTION="build"  

# Parsing command line options
while getopts ":t:p:b:ca" opt; do
  case ${opt} in
    t )
      TAG=$OPTARG
      ;;
    p )
      INSTALL_PATH=$OPTARG
      ;;
    b )
      BUILD_PATH=$OPTARG
      ;;
    c )
      ACTION="clean"
      ;;
    a ) 
      ACTION="build"
      ;;
    \? )
      echo "Invalid option: -$OPTARG" >&2
      exit 1
      ;;
  esac
done
shift $((OPTIND -1))
 
# If no TAG is specified via options, the default value is used.
if [ -z "$TAG" ]; then
  TAG=$TAG_DEFAULT
fi
 
# If INSTALL_PATH is not specified via the option, the default value is used.
if [ -z "$INSTALL_PATH" ]; then
  INSTALL_PATH=$INSTALL_PATH_DEFAULT
fi
 
# If BUILD_PATH is not specified via the option, the default value is used.
if [ -z "$BUILD_PATH" ]; then
  BUILD_PATH=$BUILD_PATH_DEFAULT
fi

#switch to the v4l-utils submodule directory
if ! cd submodules/v4l-utils/; then
    echo "cannot change directory to v4l-utils"
    exit 1
fi

# Perform the corresponding operation according to the ACTION variable
case "$ACTION" in
  "build")
    # pull the specific tag
    if ! git fetch --tags; then
        echo "can not fetch tags"
        exit 1
    fi
    if ! git checkout "$TAG"; then
        echo "can not checkout tag $TAG"
        exit 1
    fi
    # Build, specific Build_Path and Install_path, Install_Path need to use absolute path
  meson setup -Dprefix=$INSTALL_PATH $BUILD_PATH
    ninja -C $BUILD_PATH
    sudo ninja -C $BUILD_PATH install
    ;;
  "clean")
    rm -rf $BUILD_PATH
    sudo rm -rf $INSTALL_PATH $BUILD_PATH
    ;;
  *)
    echo "Invalid action specified: $ACTION"
    echo "Valid actions are: build, clean"
    exit 1
    ;;
esac
