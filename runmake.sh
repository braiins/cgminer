#!/bin/sh

set -e

#make clean
#make distclean

ROOTFS_DIR=$1
MAKE_JOBS=$2
DEBUG_CFLAGS=$3

for i in 5 6; do
    CHIP_TYPE=A$i
    sed -i "s/#define CHIP_A[0-9]/#define CHIP_A$i/g" miner.h
    ./autogen.sh
    LDFLAGS=-L${ROOTFS_DIR}/lib \
        CFLAGS="-I${ROOTFS_DIR}/include ${DEBUG_CFLAGS}" \
        LIBCURL_CFLAGS="-I${ROOTFS_DIR}/include/curl" \
        LIBCURL_LIBS="-L${ROOTFS_DIR}/lib -lcurl" \
        ./configure --prefix=/ --program-prefix=braiins-a$i \
        --enable-bitmine_${CHIP_TYPE} --without-curses --host=arm-xilinx-linux-gnueabi --build=x86_64-pc-linux-gnu # --target=arm
    make -j${MAKE_JOBS}
    make DESTDIR=${ROOTFS_DIR} install
done
