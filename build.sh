#!/bin/bash

# user need to export their own toolchain and may required to change the CC
export ARCH=arm64
export CC=aarch64-linux-gnu-gcc

${CC} vidtest.c -o v4l2-vidtest

echo "Transferring Data"



