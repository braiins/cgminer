#!/bin/sh

#make clean
#make distclean

ROOTFS_DIR=/home/public/huwt/rootfs
MAKE_JOBS=$2

# A5
#CHIP_TYPE=A5
#sed -i "s/#define CHIP_A[0-9]/#define CHIP_A5/g" miner.h
#./autogen.sh
#LDFLAGS=-L${ROOTFS_DIR}/lib \
#CFLAGS=-I${ROOTFS_DIR}/include \
#./configure --prefix=${ROOTFS_DIR} \
#--enable-bitmine_${CHIP_TYPE} --without-curses --host=arm-xilinx-linux-gnueabi --build=x86_64-pc-linux-gnu # --target=arm
#make -j${MAKE_JOBS}
#cp cgminer cgminer_${CHIP_TYPE}

# A6
#CHIP_TYPE=A6
#sed -i "s/#define CHIP_A[0-9]/#define CHIP_A6/g" miner.h
#./autogen.sh
#LDFLAGS=-L${ROOTFS_DIR}/lib \
#CFLAGS=-I${ROOTFS_DIR}/include \
#./configure --prefix=${ROOTFS_DIR} \
#--enable-bitmine_${CHIP_TYPE} --without-curses --host=arm-xilinx-linux-gnueabi --build=x86_64-pc-linux-gnu # --target=arm
make -j${MAKE_JOBS}
cp cgminer cgminer_${CHIP_TYPE}

cp ./cgminer /home/public/update/cgminer_huwt.$1
chmod 777 /home/public/update/cgminer_huwt.$1

