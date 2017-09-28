#!/bin/sh

ROOTFS_DIR=$1

# 安装json库(cgminer悲删除)
make install
rm -rf ${ROOTFS_DIR}/bin/cgminer

# 安装多版本cgminer
mv cgminer_A* ${ROOTFS_DIR}/bin/

