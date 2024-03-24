#Copyright 2017, 2021 NXP

ccflags-y := -DLA9310_HOST_SW_VERSION="${VERSION_STRING}" -I${COMMON_DIR} -I${UAPI_DIR}
ccflags-y += -Wall -Wno-unused-function -Wno-unused-variable
ccflags-y += -Wno-error

ccflags-y := -std=gnu99 -Wno-declaration-after-statement 


ifeq (${DEBUG}, 1)
ccflags-y += -g -DDEBUG
endif

ccflags-y += -I${LA9310_DRV_HEADER_DIR} -I${LA9310_COMMON_HEADERS} 
ccflags-y += -I/home/davide/granita_kernel/system  -I/home/davide/granita_kernel/core -I/home/davide/granita_kernel/api -I/home/davide/granita_kernel/kernel

ccflags-y += -I/home/davide/LMS7002M_kernel/include -I/home/davide/LMS7002M_kernel/interfaces -I/home/davide/LMS7002M_kernel/src






obj-m := la9310rfnm.o rfnm_usb_function.o rfnm_usb.o

rfnm_granita-objs := rfnm_granita_m.o
rfnm_granita-objs += rfnm_fe_generic.o 
rfnm_granita-objs += rfnm_fe_granita0.o

rfnm_granita-objs += ../../../granita_kernel/api/ErrHandling.o
rfnm_granita-objs += ../../../granita_kernel/kernel/SiKernel.o
rfnm_granita-objs += ../../../granita_kernel/system/SiSystem.o
rfnm_granita-objs += ../../../granita_kernel/api/SiGranitaP_parser.o
rfnm_granita-objs += ../../../granita_kernel/core/SiGranitaP_Core.o 




rfnm_lime-objs := rfnm_lime_m.o 
rfnm_lime-objs += rfnm_fe_generic.o 
rfnm_lime-objs += rfnm_fe_lime0.o 
rfnm_lime-objs += dtoa.o 

rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_afe.o
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_logger.o
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_tbb.o
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_cgen.o
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_nco.o 
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_time.o
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_filter_cal.o
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_rbb.o
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_trf.o
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_gfir.o
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_rfe.o
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_tx_filter_cal.o
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_impl.o
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_rx_filter_cal.o
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_txtsp.o
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_ldo.o
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_rxtsp.o 
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_vco.o
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_lml.o 
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_sxx.o 
rfnm_lime-objs += ../../../LMS7002M_kernel/src/LMS7002M_xbuf.o






obj-m += rfnm_granita.o rfnm_lime.o

obj-m += rfnm_daughterboard.o
obj-m += rfnm_gpio.o
obj-m += rfnm_gpt.o
obj-m += rfnm_tti.o

#obj-m += rfnm_kasan.o




la9310rfnm-objs := la9310_rfnm.o  cache.o #pack12bit.o unpack12bit.o

