#!/bin/sh

#make distclean
./autogen.sh

ROOTFS_DIR=$1
MAKE_JOBS=$2

LDFLAGS=-L${ROOTFS_DIR}/lib \
CFLAGS=-I${ROOTFS_DIR}/include \
./configure --prefix=${ROOTFS_DIR} \
--enable-bitmine_A1 --without-curses --host=arm-xilinx-linux-gnueabi --build=x86_64-pc-linux-gnu # --target=arm

make -j${MAKE_JOBS}

