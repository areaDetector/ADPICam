#!/bin/bash 

# This script configures areadetector R3-11
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
apt-get install -y libx11-dev libxext-dev

# The directory above
SUPPORT="$(dirname "${PWD}")"

# This won't have been set yet on the areaDetector pass.
AREA_DETECTOR=$PWD

# This file will have been written by ci-scripts cue.py
source $SUPPORT/RELEASE.local

pushd $AREA_DETECTOR/configure
echo "Copying from example."
./copyFromExample
popd

#hack config_site.local
sed -i -e "/^WITH_PVA\s*=/ s,=.*,=YES," $AREA_DETECTOR/configure/CONFIG_SITE.local
sed -i -e "/^WITH_OPENCV\s*=/ s,=.*,=NO," $AREA_DETECTOR/configure/CONFIG_SITE.local
sed -i -e "/^WITH_QSRV\s*=/ s,=.*,=NO," $AREA_DETECTOR/configure/CONFIG_SITE.local
sed -i -e "/^WITH_BITSHUFFLE\s*=/ s,=.*,=NO," $AREA_DETECTOR/configure/CONFIG_SITE.local

# EXAMPLE_CONFIG_SITE.local.linux-x86_64 sets WITH_BOOST. I don't wannit.
if [ -f $AREA_DETECTOR/configure/CONFIG_SITE.local.$EPICS_HOST_ARCH ]; then
    rm $AREA_DETECTOR/configure/CONFIG_SITE.local.$EPICS_HOST_ARCH
fi

# Hack RELEASE.local.linux-x86_64 file
# NB, this file currently points to ADPointGrey, ADEiger and ADAravis.
# I'm not installing these modules.
if [ -f $AREA_DETECTOR/configure/RELEASE.local.$EPICS_HOST_ARCH ]; then
    rm $AREA_DETECTOR/configure/RELEASE.local.$EPICS_HOST_ARCH
fi
echo EPICS_BASE=$EPICS_BASE > $AREA_DETECTOR/configure/RELEASE.local

# Hack RELEASE_LIBS.local file
sed -i -e "/^SUPPORT\s*=/ s,=.*,=$SUPPORT," $AREA_DETECTOR/configure/RELEASE_LIBS.local
sed -i -e "/^ASYN\s*=/ s,=.*,=$ASYN," $AREA_DETECTOR/configure/RELEASE_LIBS.local
sed -i -e "/^EPICS_BASE\s*=/ s,=.*,=$EPICS_BASE," $AREA_DETECTOR/configure/RELEASE_LIBS.local
sed -i -e "/^AREA_DETECTOR\s*=/ s,=.*,=$AREA_DETECTOR," $AREA_DETECTOR/configure/RELEASE_LIBS.local

# Hack RELEASE_PRODS.local file
sed -i -e "/^SUPPORT\s*=/ s,=.*,=$SUPPORT," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^CALC\s*=/ s,=.*,=$CALC," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^BUSY\s*=/ s,=.*,=$BUSY," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^SSCAN\s*=/ s,=.*,=$SSCAN," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^AUTOSAVE\s*=/ s,=.*,=$AUTOSAVE," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^SNCSEQ\s*=/ s,=.*,=$SNCSEQ," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^EPICS_BASE\s*=/ s,=.*,=$EPICS_BASE," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^ASYN\s*=/ s,=.*,=$ASYN," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^AREA_DETECTOR\s*=/ s,=.*,=$AREA_DETECTOR," $AREA_DETECTOR/configure/RELEASE_PRODS.local

# Don't think we need this.
sed -i "s/DEVIOCSTATS/#DEVIOCSTATS/g" $AREA_DETECTOR/configure/RELEASE_PRODS.local
