#!/bin/bash 

# This script installs NDS and NDS-epics
#
# check if user has right permissions
if [ "$(id -u)" != "0" ]; then
    echo "Sorry, you are not root. Please try again using sudo."
    exit 1
fi

# terminate script after first line that fails
set -e

apt-get install -y libreadline6-dev libncurses5-dev perl clang g++-mingw-w64-i686 g++-mingw-w64-x86-64 qemu-system-x86 re2c tar
apt-get install -y build-essential git python curl p7zip-full wget libxml2-dev

export GITLAB_CI=1
export SETUP_PATH=".ci-local:.ci"
export BASE_RECURSIVE="YES"
export CMP="gcc"
export BCFG="static"
export SET="stable"
export WINE=64
export VV=1
export CLEAN_DEPS="NO"
echo "PREPARE" > /tmp/adpicam_install.log
python .ci/cue.py prepare > /tmp/adpicam_install.log 2>&1
echo "BUILD" > /tmp/adpicam_install.log
python .ci/cue.py build > /tmp/adpicam_install.log 2>&1

