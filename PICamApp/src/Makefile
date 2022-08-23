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
LIB_SYS_LIBS += picam piac picc pida pidi
endif

DBD += PICamSupport.dbd

ifeq (mingw, $(findstring mingw, $(T_A)))
  # multiple definition of `_Unwind_Resume on Linux. I don't know how to fix it.
  USR_LDFLAGS += -Wl,-allow-multiple-definition
endif

include $(ADCORE)/ADApp/commonLibraryMakefile

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

