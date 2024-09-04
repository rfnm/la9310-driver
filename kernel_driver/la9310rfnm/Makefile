ccflags-y := -DLA9310_HOST_SW_VERSION="${VERSION_STRING}" -I${COMMON_DIR} -I${UAPI_DIR}
ccflags-y += -Wall -Wno-unused-function -Wno-unused-variable
ccflags-y += -Wno-error

ccflags-y := -std=gnu99 -Wno-declaration-after-statement 


ifeq (${DEBUG}, 1)
ccflags-y += -g -DDEBUG
endif

#RFNM_BUILD_DIR = /home/davide

ccflags-y += -I${LA9310_DRV_HEADER_DIR} -I${LA9310_COMMON_HEADERS} 
ccflags-y += -I${RFNM_BUILD_DIR}/la9310-driver/LimeSuiteNG/embedded/include
ccflags-y += -I${RFNM_BUILD_DIR}/g_icewings/system  -I${RFNM_BUILD_DIR}/g_icewings/core -I${RFNM_BUILD_DIR}/g_icewings/api -I${RFNM_BUILD_DIR}/g_icewings/kernel
ccflags-y += -I${RFNM_BUILD_DIR}/la9310-driver/kernel_driver/la9310rfnm
ccflags-y += -I${KERNEL_DIR}




obj-m := la9310rfnm.o rfnm_usb_function.o rfnm_usb.o 

rfnm_granita-objs := rfnm_granita_m.o
rfnm_granita-objs += rfnm_fe_generic.o 
rfnm_granita-objs += rfnm_fe_granita0.o
rfnm_granita-objs += rfnm_granita_rffc.o

rfnm_granita-objs += ../../../g_icewings/api/ErrHandling.o
rfnm_granita-objs += ../../../g_icewings/kernel/SiKernel.o
rfnm_granita-objs += ../../../g_icewings/system/SiSystem.o
rfnm_granita-objs += ../../../g_icewings/api/SiIceWing_parser.o
rfnm_granita-objs += ../../../g_icewings/core/SiIceWing_Core.o 




rfnm_lime-objs := rfnm_lime_m.o 
rfnm_lime-objs += rfnm_fe_generic.o 
rfnm_lime-objs += rfnm_fe_lime0.o 

rfnm_lime-objs += ../../LimeSuiteNG/embedded/lms7002m/lms7002m.o
rfnm_lime-objs += ../../LimeSuiteNG/embedded/lms7002m/privates.o

rfnm_breakout-objs := rfnm_breakout_m.o 

# comment out to disable granita module
# obj-m += rfnm_granita.o 

obj-m += rfnm_lime.o rfnm_breakout.o


obj-m += rfnm_daughterboard.o
obj-m += rfnm_gpio.o
obj-m += rfnm_gpt.o
obj-m += rfnm_tti.o
#obj-m += rfnm_test.o
obj-m += rfnm_usb_boost.o
#obj-m += rfnm_kasan.o
obj-m += rfnm_lalib.o

la9310rfnm-objs := la9310_rfnm.o rfnm_neon.o cache.o pack16to12.o unpack12to16.o

CFLAGS_REMOVE_rfnm_neon.o += -mgeneral-regs-only
CFLAGS_REMOVE_rfnm_granita_rffc.o += -mgeneral-regs-only
CFLAGS_REMOVE_../../../g_icewings/system/SiSystem.o += -mgeneral-regs-only