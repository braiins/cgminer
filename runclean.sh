#!/bin/sh


if [ -f Makefile ]; then
    make clean
    make distclean
fi

# compat 清理不干净
git co compat

