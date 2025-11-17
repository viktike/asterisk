#!/bin/sh
./bootstrap
./configure --with-asterisk=/usr/src/asterisk --prefix=/usr
make install
