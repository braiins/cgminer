#!/bin/sh

ROOTFS_DIR=$1

make DESTDIR=${ROOTFS_DIR} install
