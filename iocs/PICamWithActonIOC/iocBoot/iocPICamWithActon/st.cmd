errlogInit(20000)

< envPaths
#epicsThreadSleep(20)
dbLoadDatabase("$(TOP)/dbd/PICamWithActonApp.dbd")
PICamWithActonApp_registerRecordDeviceDriver(pdbbase) 

epicsEnvSet("PREFIX",    "13PICAM1:")
epicsEnvSet("PORT",      "PICAMDET1")
epicsEnvSet("SPECPORT",  "PICAMSPEC1")
epicsEnvSet("QSIZE",     "20")
epicsEnvSet("XSIZE",     "2048")
epicsEnvSet("YSIZE",     "2048")
epicsEnvSet("NCHANS",    "2048")
# The maximum number of frames buffered in the NDPluginCircularBuff plugin
epicsEnvSet("CBUFFS", "500")

# The search path for database files
epicsEnvSet("EPICS_DB_INCLUDE_PATH", "$(ADCORE)/db")

# Create a PICam driver
# PICamConfig(const char *portName, IDType, IDValue, maxBuffers, size_t maxMemory, int priority, int stackSize)

# This will select a live camera.
PICamConfig("$(PORT)", 0, 0, 0, 0, 12546318)

asynSetTraceIOMask($(PORT), 0, 2)
#asynSetTraceMask($(PORT),0,0xff)

dbLoadRecords("$(ADPICAM)/db/PICam.template","P=$(PREFIX),R=cam1:,PORT=$(PORT),ADDR=0,TIMEOUT=1")
dbLoadRecords("$(ADPICAM)/db/ARC_Instrument.template", "P=$(PREFIX):,PORT=$(SPECPORT),ADDR=0")

# Create a standard arrays plugin, set it to get data from Driver.
NDStdArraysConfigure("Image1", 3, 0, "$(PORT)", 0)
ARC_InstrumentPortDriverConfigure($(SPECPORT), 0)
dbLoadRecords("NDStdArrays.template", "P=$(PREFIX),R=image1:,PORT=Image1,ADDR=0,TIMEOUT=1,NDARRAY_PORT=$(PORT),TYPE=Int16,SIZE=16,FTVL=SHORT,NELEMENTS=20000000")

# Load all other plugins using commonPlugins.cmd
< $(ADCORE)/iocBoot/commonPlugins.cmd

#asynSetTraceMask($(PORT),0,0x09)
iocInit()
