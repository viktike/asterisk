#!/bin/sh
aclocal
autoheader
autoconf
automake --add-missing
CFLAGS="-DAST_MODULE_SELF_SYM=__chan_dongle_self" ./configure --enable-manager --enable-apps --with-asterisk=/usr/src/asterisk/include
CFLAGS="-DAST_MODULE_SELF_SYM=__chan_dongle_self" make install
