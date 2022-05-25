TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

ifeq (windows-x64, $(findstring windows-x64, $(T_A)))
USR_CPPFLAGS += -D__X64
endif

LIBRARY_IOC_WIN32 = ADPICam
LIBRARY_IOC_Linux = ADPICam
LIB_SRCS += ADPICam.cpp

ifeq (win32-x86, $(findstring win32-x86, $(T_A)))
LIB_LIBS += Picam32
else ifeq (windows-x64, $(findstring windows-x64, $(T_A)))
LIB_LIBS += Picam
else
LIB_SYS_LIBS += picam
endif

DBD += PICamSupport.dbd

include $(ADCORE)/ADApp/commonLibraryMakefile

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

