
ifeq ($(COLLECTOR_BUILDER_TAG),)
COLLECTOR_BUILDER_TAG=cache
endif

ifeq ($(COLLECTOR_TAG),)
ifeq ($(CIRCLE_TAG),)
COLLECTOR_TAG=$(shell git describe --tags --abbrev=10 --dirty)
else
COLLECTOR_TAG := $(CIRCLE_TAG)
endif
endif

USE_VALGRIND ?= false
ADDRESS_SANITIZER ?= false
CMAKE_BUILD_TYPE ?= Release
COLLECTOR_APPEND_CID ?= false
PLATFORM ?= linux/amd64
TRACE_SINSP_EVENTS ?= false

COLLECTOR_BUILD_CONTEXT = collector/


ifeq ($(USE_HELGRIND),true)
	COLLECTOR_PRE_ARGUMENTS := valgrind --tool=helgrind
	USE_VALGRIND := true
else ifeq ($(USE_VALGRIND),true)
	COLLECTOR_PRE_ARGUMENTS := valgrind --leak-check=full
endif

export COLLECTOR_PRE_ARGUMENTS
