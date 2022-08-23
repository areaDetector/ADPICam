TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

PROD_NAME = PICamWithActonApp
PROD_IOC_WIN32 += $(PROD_NAME)

# <name>.dbd will be created from <name>Include.dbd
DBD += $(PROD_NAME).dbd
$(PROD_NAME)_DBD += PICamSupport.dbd
$(PROD_NAME)_DBD_WIN32 += ARC_Instrument.dbd

SRC_DIRS += $(PRINCETONSPECTRO)

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
$(PROD_NAME)_SRCS += $(PROD_NAME)_registerRecordDeviceDriver.cpp $(PROD_NAME)Main.cpp

# Add locally compiled object code
$(PROD_NAME)_LIBS += ADPICam

ifeq (windows-x64, $(findstring windows-x64, $(T_A)))
$(PROD_NAME)_LIBS += Picam
$(PROD_NAME)_LIBS += ARC_InstrumentDev
else ifeq (win32-x86, $(findstring win32-x86, $(T_A)))
$(PROD_NAME)_LIBS += Picam32

endif

ifeq (mingw, $(findstring mingw, $(T_A)))
  # static build only, multiple definition of pthread_create, pthread_cond_init, etc on Linux.
  # I don't know how to fix it.
  USR_LDFLAGS += -Wl,-allow-multiple-definition
endif

include $(ADCORE)/ADApp/commonDriverMakefile

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

