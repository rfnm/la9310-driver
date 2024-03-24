#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/dma-mapping.h>
#include <la9310_base.h>
//#include "rfnm.h"
//#include "rfnm_callback.h"
#include <asm/cacheflush.h>

#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-mapping.h>


#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/list.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>


#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/usb/composite.h>
#include <linux/err.h>

#include <linux/delay.h>

#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

//#include "rfnm_types.h"
#include <linux/rfnm-shared.h>
#include <linux/rfnm-gpio.h>

volatile unsigned int *gpio[7];

struct rfnm_gpio_map_bit {
	uint8_t bank[2];
	uint8_t num[2];
};

void rfnm_gpio_clear(uint8_t dgb_id, uint32_t gpio_map_id) {

	int bank, num;

	if(dgb_id == 0) {
		bank = (gpio_map_id >> R_DBG_S_PRI_BANK) & 0xff;
		num = (gpio_map_id >> R_DBG_S_PRI_NUM) & 0xff;
	} else {
		bank = (gpio_map_id >> R_DBG_S_SEC_BANK) & 0xff;
		num = (gpio_map_id >> R_DBG_S_SEC_NUM) & 0xff;
	}

	if(bank == 6) {
		// la9310
		*(gpio[bank] + 2) &= ~(1 << num);
 	} else {
		// imx
		*gpio[bank] &= ~(1 << num);
	}
}
EXPORT_SYMBOL(rfnm_gpio_clear);

void rfnm_gpio_set(uint8_t dgb_id, uint32_t gpio_map_id) {

	int bank, num;

	if(dgb_id == 0) {
		bank = (gpio_map_id >> R_DBG_S_PRI_BANK) & 0xff;
		num = (gpio_map_id >> R_DBG_S_PRI_NUM) & 0xff;
	} else {
		bank = (gpio_map_id >> R_DBG_S_SEC_BANK) & 0xff;
		num = (gpio_map_id >> R_DBG_S_SEC_NUM) & 0xff;
	}

	if(bank == 6) {
		// la9310
		*(gpio[bank] + 2) |= (1 << num);
 	} else {
		// imx
		*gpio[bank] |= (1 << num);
	}
}
EXPORT_SYMBOL(rfnm_gpio_set);

void rfnm_gpio_output(uint8_t dgb_id, uint32_t gpio_map_id) {

	int bank, num;

	if(dgb_id == 0) {
		bank = (gpio_map_id >> R_DBG_S_PRI_BANK) & 0xff;
		num = (gpio_map_id >> R_DBG_S_PRI_NUM) & 0xff;
	} else {
		bank = (gpio_map_id >> R_DBG_S_SEC_BANK) & 0xff;
		num = (gpio_map_id >> R_DBG_S_SEC_NUM) & 0xff;
	}

	if(bank == 6) {
		// la9310
		*(gpio[bank]) |= (1 << num);
 	} else {
		// imx
		*(gpio[bank] + 0x1) |= (1 << num);
	}
}
EXPORT_SYMBOL(rfnm_gpio_output);

static __init int rfnm_gpio_init(void)
{
	void __iomem *gpio_iomem;

	// imx
	gpio_iomem = ioremap(0x30200000, SZ_4K);
	gpio[1] = (volatile unsigned int *) gpio_iomem;
	gpio_iomem = ioremap(0x30210000, SZ_4K);
	gpio[2] = (volatile unsigned int *) gpio_iomem;
	gpio_iomem = ioremap(0x30220000, SZ_4K);
	gpio[3] = (volatile unsigned int *) gpio_iomem;
	gpio_iomem = ioremap(0x30230000, SZ_4K);
	gpio[4] = (volatile unsigned int *) gpio_iomem;
	gpio_iomem = ioremap(0x30240000, SZ_4K);
	gpio[5] = (volatile unsigned int *) gpio_iomem;

	// la9310
	gpio_iomem = ioremap((RFNM_LA_BAR0_PHY_ADDR + 0x2300000), SZ_4K);
	gpio[6] = (volatile unsigned int *) gpio_iomem;


#if 0
	int list[] = {
			RFNM_DGB_GPIO4_0,
			RFNM_DGB_GPIO4_1,
			RFNM_DGB_GPIO4_2,
			RFNM_DGB_GPIO4_3 ,
			RFNM_DGB_GPIO4_4,
			RFNM_DGB_GPIO4_5,
			RFNM_DGB_GPIO4_6,
			RFNM_DGB_GPIO4_7 ,
			RFNM_DGB_GPIO5_16,
			RFNM_DGB_GPIO3_21,
			RFNM_DGB_GPIO4_8,
			RFNM_DGB_GPIO4_9,
			RFNM_DGB_GPIO2_3,
			RFNM_DGB_GPIO2_2};

	int i;
	for(i = 0; i < 14; i++) {		
		rfnm_gpio_output(0, list[i]);
		rfnm_gpio_output(1, list[i]);
		rfnm_gpio_clear(0, list[i]);
		rfnm_gpio_clear(1, list[i]);
	}
#endif

	return 0;
}

static __exit void rfnm_gpio_exit(void)
{
}


MODULE_PARM_DESC(device, "RFNM GPIO Driver");
module_init(rfnm_gpio_init);
module_exit(rfnm_gpio_exit);
MODULE_LICENSE("GPL");
