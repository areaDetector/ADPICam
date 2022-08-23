#!/bin/bash 

# This script installs areadetector R3-10
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

EPICS_ROOT=/builds/DATAnet/adpicam
SUPPORT=$EPICS_ROOT/.cache

# This won't have been set yet on the ADCore pass.
ADCORE=$PWD

# This file will have been written by ci-scripts cue.py
source $SUPPORT/RELEASE.local
# The file needs to be copied into the areaDetector folder for sub-module build.
cp $SUPPORT/RELEASE.local $AREA_DETECTOR/

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

# Hack RELEASE.local file
sed -i -e "/^ADSUPPORT\s*=/ s,=.*,=$ADSUPPORT," $AREA_DETECTOR/configure/RELEASE.local

# Hack RELEASE.local.linux-x86_64 file
# NB, this file currently points to ADPointGrey, ADEiger and ADAravis.
# I'm not installing these modules.
if [ -f $AREA_DETECTOR/configure/RELEASE.local.$EPICS_HOST_ARCH ]; then
    rm $AREA_DETECTOR/configure/RELEASE.local.$EPICS_HOST_ARCH
fi
echo EPICS_BASE=$EPICS_BASE > $AREA_DETECTOR/configure/RELEASE.local

# Hack RELEASE_LIBS file
sed -i -e "/^SUPPORT\s*=/ s,=.*,=$SUPPORT," $AREA_DETECTOR/configure/RELEASE_LIBS.local
sed -i -e "/^ASYN\s*=/ s,=.*,=$ASYN," $AREA_DETECTOR/configure/RELEASE_LIBS.local
sed -i -e "/^EPICS_BASE\s*=/ s,=.*,=$EPICS_BASE," $AREA_DETECTOR/configure/RELEASE_LIBS.local
sed -i -e "/^AREA_DETECTOR\s*=/ s,=.*,=$AREA_DETECTOR," $AREA_DETECTOR/configure/RELEASE_LIBS.local
sed -i -e "/^ADSUPPORT\s*=/ s,=.*,=$ADSUPPORT," $AREA_DETECTOR/configure/RELEASE_LIBS.local
sed -i -e "/^ADCORE\s*=/ s,=.*,=$ADCORE," $AREA_DETECTOR/configure/RELEASE_LIBS.local

# Hack RELEASE_PRODS file
sed -i -e "/^SUPPORT\s*=/ s,=.*,=$SUPPORT," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^CALC\s*=/ s,=.*,=$CALC," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^BUSY\s*=/ s,=.*,=$BUSY," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^SSCAN\s*=/ s,=.*,=$SSCAN," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^AUTOSAVE\s*=/ s,=.*,=$AUTOSAVE," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^SNCSEQ\s*=/ s,=.*,=$SNCSEQ," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^EPICS_BASE\s*=/ s,=.*,=$EPICS_BASE," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^ASYN\s*=/ s,=.*,=$ASYN," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^AREA_DETECTOR\s*=/ s,=.*,=$AREA_DETECTOR," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^ADSUPPORT\s*=/ s,=.*,=$ADSUPPORT," $AREA_DETECTOR/configure/RELEASE_PRODS.local
sed -i -e "/^ADCORE\s*=/ s,=.*,=$ADCORE," $AREA_DETECTOR/configure/RELEASE_PRODS.local

# Don't think we need this.
sed -i "s/DEVIOCSTATS/#DEVIOCSTATS/g" $AREA_DETECTOR/configure/RELEASE_PRODS.local

# These are example plugin files, but we will just use them
if [ ! -f $ADCORE/iocBoot/commonPlugins.cmd ]; then
    cp $ADCORE/iocBoot/EXAMPLE_commonPlugins.cmd $ADCORE/iocBoot/commonPlugins.cmd
fi

# Hack commonPlugins.cmd - we need sseq_settings.req, which is in the calc module
sed -i "s/#set_requestfile_path(\"\$(CALC)\/calcApp\/Db\")/set_requestfile_path(\"\$(CALC)\/calcApp\/Db\")/" $ADCORE/iocBoot/commonPlugins.cmd
if [ ! -f $ADCORE/iocBoot/commonPlugin_settings.req ]; then
    cp $ADCORE/iocBoot/EXAMPLE_commonPlugin_settings.req $ADCORE/iocBoot/commonPlugin_settings.req
fi
