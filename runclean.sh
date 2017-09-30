#!/bin/sh


if [ -f Makefile ]; then
    make clean
    make distclean
fi

# compat 清理不干净
git co compat

rm -rf compat/jansson-2.9/jansson_private_config.h.in
rm -rf compat/jansson-2.9/test-driver
rm -rf cgminer_A5 cgminer_A6
