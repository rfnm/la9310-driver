#Copyright 2017, 2021 NXP

CC              = $(CROSS_COMPILE)gcc
AR              = $(CROSS_COMPILE)ar
LD              = $(CROSS_COMPILE)ld

KERNEL_DIR ?=

ifdef SYSROOT_PATH
LDFLAGS := --sysroot=${SYSROOT_PATH}
CFLAGS := --sysroot=${SYSROOT_PATH}
endif

VERSION_STRING :=\"v0.1\"

INSTALL_DIR ?= ${PWD}/install
LIB_INSTALL_DIR := ${INSTALL_DIR}/usr/lib
BIN_INSTALL_DIR := ${INSTALL_DIR}/usr/bin
MODULE_INSTALL_DIR := ${INSTALL_DIR}/kernel_module
CONFIG_INSTALL_DIR := ${INSTALL_DIR}/etc/config/la9310_config/
SCRIPTS_INSTALL_DIR := ${INSTALL_DIR}/usr/bin
FIRMWARE_INSTALL_DIR := ${INSTALL_DIR}/lib/firmware

COMMON_DIR := ${PWD}/common/
API_DIR := ${PWD}/api/
UAPI_DIR := ${PWD}/uapi/
LA9310_DRV_HEADER_DIR := ${PWD}/kernel_driver/la9310shiva/

INCLUDES += -I${API_DIR}/ 
COMMON_INCLUDES += -I${COMMON_DIR}

CFLAGS += ${COMMON_INCLUDES}
HOST_CFLAGS += ${COMMON_INCLUDES}

CFLAGS += -Wall -DLA931x_HOST_SW_VERSION="${VERSION_STRING}" -Wno-unused-function -Wno-unused-variable

# Config Tweak handles
DEBUG ?= 1
BOOTROM_USE_EDMA ?= 1

export CC LIB_INSTALL_DIR BIN_INSTALL_DIR SCRIPTS_INSTALL_DIR MODULE_INSTALL_DIR \
	FIRMWARE_INSTALL_DIR
export CONFIG_INSTALL_DIR API_DIR KERNEL_DIR COMMON_DIR UAPI_DIR LA9310_DRV_HEADER_DIR
export INCLUDES CFLAGS VERSION_STRING LDFLAGS
export DEBUG BOOTROM_USE_EDMA

ifeq (${DEBUG}, 1)
CFLAGS += -DDEBUG
endif

#DIRS ?= lib app kernel_driver firmware scripts
DIRS ?= kernel_driver

CLEAN_DIRS = $(patsubst %, %_clean, ${DIRS})
INSTALL_DIRS = $(patsubst %, %_install, ${DIRS})

.PHONY: ${DIRS} ${CLEAN_DIRS} ${INSTALL_DIRS}

default: all

${DIRS}:
	${MAKE} -C $@ all

all: ${DIRS}

${CLEAN_DIRS}:
	${MAKE} -C $(patsubst %_clean, %, $@) clean
	rm -rf ${LIB_INSTALL_DIR}/*;
	rm -rf ${BIN_INSTALL_DIR}/*;
	rm -rf ${SCRIPTS_INSTALL_DIR}/*;
	rm -rf ${INSTALL_DIR};

${INSTALL_DIRS}:
	${MAKE} -j1 -C $(patsubst %_install, %, $@) install

install: ${INSTALL_DIRS}

clean: ${CLEAN_DIRS}
