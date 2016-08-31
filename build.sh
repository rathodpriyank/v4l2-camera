#!/bin/bash

# user need to export their own toolchain and may required to change the CC
export ARCH=arm64
export CC=aarch64-linux-gnu-gcc

${CC} v4l2-test-app.c -o v4l2-test-app
${CC} vidtest.c -o v4l2-vidtest

echo "Transferring Data"



