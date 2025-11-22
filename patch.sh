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

# Compile manually:
#    app_konference.so
#    app_tiresias.so
#    app_voicechanger.so
#    chan_capi.so
#    chan_dongle.so
#    chan_quectel.so
#    chan_sccp.so
#    res_speech_vosk.so


########
# RHEL #
# ######
#dnf install jemalloc jemalloc-devel
#LDFLAGS="-L/usr/lib64 -ljemalloc -Wl,--no-as-needed" ./configure
#LDFLAGS="-L/usr/lib64 -ljemalloc -Wl,--no-as-needed" make -j4 install
#MALLOC_CONF="narenas:1" asterisk -cvvv

##########
# Debian #
##########
#apt-get install libjemalloc2 libjemalloc2-dev libgoogle-perftools-dev
#LDFLAGS="-L/lib/x86_64-linux-gnu -ltcmalloc -Wl,--no-as-needed" ./configure
#LDFLAGS="-L/lib/x86_64-linux-gnu -ltcmalloc -Wl,--no-as-needed" make -j4 install
#MALLOC_CONF="narenas:1" TCMALLOC_RELEASE_RATE=0.1 TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES=268435456 asterisk -cvvv
