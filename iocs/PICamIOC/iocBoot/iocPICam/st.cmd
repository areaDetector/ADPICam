errlogInit(20000)

< envPaths
#epicsThreadSleep(20)
dbLoadDatabase("$(TOP)/dbd/PICamApp.dbd")
PICamApp_registerRecordDeviceDriver(pdbbase) 

epicsEnvSet("PREFIX", "13PICAM1:")
epicsEnvSet("PORT",   "PICAMDET1")
epicsEnvSet("QSIZE",  "20")
epicsEnvSet("XSIZE",  "2048")
epicsEnvSet("YSIZE",  "2048")
epicsEnvSet("NCHANS", "2048")
# The maximum number of frames buffered in the NDPluginCircularBuff plugin
epicsEnvSet("CBUFFS", "500")

# The search path for database files
epicsEnvSet("EPICS_DB_INCLUDE_PATH", "$(ADCORE)/db")

# Create a PICam driver
# PICamConfig(const char *portName, IDType, IDValue, maxBuffers, size_t maxMemory, int priority, int stackSize)

# This will select a live camera.
PICamConfig("$(PORT)", 0, 0, 0, 0, 0)

# These will select a simulated demo camera.
#PICamAddDemoCamera("PIXIS: 100F")
#PICamAddDemoCamera("Quad-RO: 4320")
#PICamAddDemoCamera("PI-MAX4: 2048B-RF")

asynSetTraceIOMask($(PORT), 0, 2)
#asynSetTraceMask($(PORT),0,0xff)

dbLoadRecords("$(ADCORE)/db/ADBase.template", "P=$(PREFIX),R=cam1:,PORT=$(PORT),ADDR=0,TIMEOUT=1")
dbLoadRecords("$(ADPICAM)/db/PICam.template","P=$(PREFIX),R=cam1:,PORT=$(PORT),ADDR=0,TIMEOUT=1")

# Create a standard arrays plugin, set it to get data from Driver.
NDStdArraysConfigure("Image1", 3, 0, "$(PORT)", 0)
dbLoadRecords("$(ADCORE)/db/NDPluginBase.template","P=$(PREFIX),R=image1:,PORT=Image1,ADDR=0,TIMEOUT=1,NDARRAY_PORT=$(PORT),NDARRAY_ADDR=0")
dbLoadRecords("$(ADCORE)/db/NDStdArrays.template", "P=$(PREFIX),R=image1:,PORT=Image1,ADDR=0,TIMEOUT=1,NDARRAY_PORT=$(PORT),TYPE=Int16,SIZE=16,FTVL=SHORT,NELEMENTS=20000000")

# Load all other plugins using commonPlugins.cmd
< $(ADCORE)/iocBoot/commonPlugins.cmd
set_requestfile_path("$(ADPICAM)/PICamApp/Db")

#asynSetTraceMask($(PORT),0,0x09)
iocInit()

# save things every thirty seconds
create_monitor_set("auto_settings.req", 30, "P=$(PREFIX)")

