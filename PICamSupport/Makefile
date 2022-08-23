TOP=../
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

INC += picam.h
INC += picam_advanced.h
INC += picam_em_calibration.h
INC += picam_special.h
INC += pil_platform.h

ifeq (windows-x64, $(findstring windows-x64, $(T_A)))
LIB_INSTALLS_WIN32    += ../os/windows-x64/Picam.lib
else ifeq (win32-x86, $(findstring win32-x86, $(T_A)))
LIB_INSTALLS_WIN32    += ../os/windows-x86/Picam32.lib
else
# These are Princeton libraries
LIB_INSTALLS_Linux    += ../os/linux-x64/libpiac.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libpicam.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libpicc.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libpida.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libpidi.so
# These are Pleora libraries
LIB_INSTALLS_Linux    += ../os/linux-x64/libGCBase_gcc40_v2_4.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libGenApi_gcc40_v2_4.so
LIB_INSTALLS_Linux    += ../os/linux-x64/liblog4cpp_gcc40_v2_4.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libLog_gcc40_v2_4.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libMathParser_gcc40_v2_4.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libEbTransportLayerLib.so.5.1.7.3988
LIB_INSTALLS_Linux    += ../os/linux-x64/libEbTransportLayerLib.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libEbUtilsLib.so.5.1.7.3988
LIB_INSTALLS_Linux    += ../os/linux-x64/libEbUtilsLib.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libftd2xx.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libPtConvertersLib.so.5.1.7.3988
LIB_INSTALLS_Linux    += ../os/linux-x64/libPtConvertersLib.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libPtUtilsLib.so.5.1.7.3988
LIB_INSTALLS_Linux    += ../os/linux-x64/libPtUtilsLib.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvAppUtils.so.4.1.7.3988
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvAppUtils.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvBase.so.4.1.7.3988
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvBase.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvBuffer.so.4.1.7.3988
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvBuffer.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvDevice.so.4.1.7.3988
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvDevice.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvGenICam.so.4.1.7.3988
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvGenICam.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvGUI.so.4.1.7.3988
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvGUI.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvPersistence.so.4.1.7.3988
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvPersistence.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvSerial.so.4.1.7.3988
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvSerial.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvStream.so.4.1.7.3988
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvStream.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvTransmitter.so.4.1.7.3988
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvTransmitter.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvVirtualDevice.so.4.1.7.3988
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvVirtualDevice.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libPvSerial.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libSimpleImagingLib.so.4.1.7.3988
LIB_INSTALLS_Linux    += ../os/linux-x64/libSimpleImagingLib.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libXalan-C_gcc40_v1_10_1.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libXalanMessages_gcc40_v1_10_1.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libXerces-C_gcc40_v2_7_1.so
LIB_INSTALLS_Linux    += ../os/linux-x64/libXMLLoader_gcc40_v2_4.so
endif

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

