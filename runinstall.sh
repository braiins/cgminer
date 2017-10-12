#!/bin/sh

ROOTFS_DIR=$1

# 安装json库(删除cgminer)
make install
rm -rf ${ROOTFS_DIR}/bin/cgminer

# 安装多版本cgminer
mv innominer_* ${ROOTFS_DIR}/bin/

