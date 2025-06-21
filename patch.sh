#!/usr/bin/env bash
DIR="$( dirname -- "${BASH_SOURCE[0]}"; )";
cd ${DIR}

# Version handling
export MAINLINE_BRANCH=20
echo "20.0.0" > .version
echo "Fix" > .flavor


# Dependencies
sudo ./contrib/scripts/install_prereq install


# Autoconf
#autoreconf --install --force

#aclocal
#autoconf
#autoheader
#automake --add-missing

./bootstrap.sh

# Download
./contrib/scripts/get_mp3_source.sh
./contrib/scripts/get_ilbc_source.sh
