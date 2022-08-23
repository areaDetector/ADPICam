#Makefile at top of application tree

TOP = .
include $(TOP)/configure/CONFIG
ifdef PRINCETONSPECTRO
  DIRS := $(DIRS) $(filter-out $(DIRS), configure)
  DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *App))
  DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *app))
  DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard iocBoot))
endif
include $(TOP)/configure/RULES_TOP
