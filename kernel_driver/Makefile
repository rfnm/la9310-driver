#Copyright 2017, 2021 NXP

PWD=$(shell pwd)

ifndef KERNEL_DIR
$(error KERNEL_DIR is not set)
endif

ifndef LA9310_COMMON_HEADERS
$(error LA9310_COMMON_HEADERS is not set)
endif

include $(KERNEL_DIR)/.config

obj-y += la9310shiva/
obj-y += la9310demo/
obj-y += la9310rfnm/

all:
	make ARCH=$(ARCH) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	make ARCH=$(ARCH) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -rf ${MODULE_INSTALL_DIR}/*.ko

install:
	install  -D  la9310shiva/la9310shiva.ko ${MODULE_INSTALL_DIR}/la9310shiva.ko
	install  -D  la9310demo/la9310demo.ko ${MODULE_INSTALL_DIR}/la9310demo.ko
	install  -D  la9310rfnm/la9310rfnm.ko ${MODULE_INSTALL_DIR}/la9310rfnm.ko