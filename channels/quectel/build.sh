#!/bin/sh
./bootstrap
CFLAGS="-DAST_MODULE_SELF_SYM=__chan_quectel_self" ./configure --enable-manager --enable-apps --with-asterisk=/usr/src/asterisk/include --with-astversion=14
CFLAGS="-DAST_MODULE_SELF_SYM=__chan_quectel_self" make install
