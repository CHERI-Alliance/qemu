#!/bin/bash

TARGET=$1
NINJA_TARGET=qemu-system-${TARGET}
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
REPO_DIR=$(realpath ${SCRIPT_DIR}/../..)
BUILD_DIR=${REPO_DIR}/build_${TARGET}
MESON="/usr/bin/python3 -B $REPO_DIR/meson/meson.py"
which meson && MESON=$(which meson)
pushd $BUILD_DIR
$MESON test --suite qemu
popd

pushd $SCRIPT_DIR
qemu_bin=${REPO_DIR}/build_${TARGET}/${NINJA_TARGET} ./${TARGET}_test.sh -j -v -w
mv $SCRIPT_DIR/${TARGET}_tests.xml $BUILD_DIR
popd
# at the end of this we will expect results in $BUILD_DIR/${TARGET}.xml and $BUILD_DIR/meson-logs/testlog.junit.xml
