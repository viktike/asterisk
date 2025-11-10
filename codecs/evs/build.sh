#!/bin/sh
CFLAGS="-DNDEBUG -fPIC" DEBUG=0 RELEASE=1 make
cc -shared -o lib3gpp-evs.so build/*.o
mkdir -p /usr/lib/3gpp-evs
cp lib3gpp-evs.so /usr/lib/3gpp-evs/
ldconfig
mkdir -p /usr/include/3gpp-evs
cp lib_*/*.h /usr/include/3gpp-evs/
