#!/bin/bash 

# This script installs ADCore R3-11
#
# check if user has right permissions
if [ "$(id -u)" != "0" ]; then
    echo "Sorry, you are not root. Please try again using sudo."
    exit 1
fi

# terminate script after first line that fails
set -e

if [ -z "$EPICS_HOST_ARCH" ]
then
    EPICS_HOST_ARCH=linux-x86_64
fi

# The directory above
SUPPORT="$(dirname "${PWD}")"

# This won't have been set yet on the ADCore pass.
ADCORE=$PWD

# This file will have been written by ci-scripts cue.py
source $SUPPORT/RELEASE.local

# Hack RELEASE_LIBS.local file
sed -i -e "/^ADCORE\s*=/ s,=.*,=$ADCORE," $AREA_DETECTOR/configure/RELEASE_LIBS.local

# Hack RELEASE_PRODS.local file
sed -i -e "/^ADCORE\s*=/ s,=.*,=$ADCORE," $AREA_DETECTOR/configure/RELEASE_PRODS.local

# These are example plugin files, but we will just use them
if [ ! -f $ADCORE/iocBoot/commonPlugins.cmd ]; then
    cp $ADCORE/iocBoot/EXAMPLE_commonPlugins.cmd $ADCORE/iocBoot/commonPlugins.cmd
fi

# Hack commonPlugins.cmd - we need sseq_settings.req, which is in the calc module
sed -i "s/#set_requestfile_path(\"\$(CALC)\/calcApp\/Db\")/set_requestfile_path(\"\$(CALC)\/calcApp\/Db\")/" $ADCORE/iocBoot/commonPlugins.cmd
if [ ! -f $ADCORE/iocBoot/commonPlugin_settings.req ]; then
    cp $ADCORE/iocBoot/EXAMPLE_commonPlugin_settings.req $ADCORE/iocBoot/commonPlugin_settings.req
fi

if [ "$WINE" == "64" ]; then
    if [ ! -f /usr/bin/x86_64-w64-mingw32-g++-posix ]; then
        apt-get install -y g++-mingw-w64-x86-64
    fi
    # see https://github.com/randombit/botan/issues/2039
    update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
elif [ "$WINE" == "32" ]; then
    if [ ! -f /usr/bin/i686-w64-mingw32-g++-posix ]; then
        apt-get install -y g++-mingw-w64-i686
    fi
    update-alternatives --set i686-w64-mingw32-g++ /usr/bin/i686-w64-mingw32-g++-posix
fi

