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
#include <linux/delay.h>

#include <linux/rfnm-shared.h>
#include <linux/rfnm-gpio.h>

#include "rfnm_fe_generic.h"
#include "rfnm_fe_granita0.h"

#include "SiSystem.h"
#include "SiGranitaP_parser.h"
#include "SiGranitaP_Core.h"
#include "SiDrv.h"

//#include "fe_generic.h"
//#include "fe_granita.h"

char SiCoreChar[3] = {'a', 'b', 0};

/*
void __iomem *gpio4_iomem;
volatile unsigned int *gpio4;
int gpio4_initial;

struct rfnm_granita_spi {
	struct spi_controller *ctlr;
	void __iomem *base;
};*/

/*struct __attribute__((__packed__)) rfnm_packet_head {
		uint32_t check;
	uint32_t cc;
	uint8_t reader_too_slow;
	uint8_t padding[16 - 9 + 4 + 12];
};*/



/*void rfnm_granita_register() {
	rfnm_daughterboard_register_cb()
}*/

int parse_granita_iq_lpf(int mhz) {
	if(mhz >= 400) {
		return 400000;
	} else if(mhz >= 320) {
		return 320000;
	} else if(mhz >= 200) {
		return 200000;
	} else if(mhz >= 160) {
		return 160000;
	} else if(mhz >= 100) {
		return 100000;
	} else if(mhz >= 80) {
		return 80000;
	} else if(mhz >= 60) {
		return 60000;
	} else if(mhz >= 40) {
		return 40000;
	} else if(mhz >= 20) {
		return 20000;
	} else {
		if(mhz == 0) {
			return 20000;
		} else {
			return 100000;
		}
	}
}


void rfnm_tx_ch_get(struct rfnm_dgb *dgb_dt, struct rfnm_api_tx_ch * tx_ch) {
	printk("inside rfnm_tx_ch_get\n");
}
void rfnm_rx_ch_get(struct rfnm_dgb *dgb_dt, struct rfnm_api_rx_ch * rx_ch) {
	printk("inside rfnm_rx_ch_get\n");
}



int rfnm_tx_ch_set(struct rfnm_dgb *dgb_dt, struct rfnm_api_tx_ch * tx_ch) {


	int freq_mhz = tx_ch->freq / (1000 * 1000);
	int ret;

	ret = SiAPIPowerUpTX(SiCoreChar[dgb_dt->dgb_id], 1, freq_mhz * 1000, parse_granita_iq_lpf(tx_ch->iq_lpf_bw));
	if(ret) {
		printk("SiAPIPowerUpTX wouldn't return nice things\n");
		return -1;
	}

	ret = SiAPITXGAIN(SiCoreChar[dgb_dt->dgb_id], 1, 48);
	if(ret) {
		printk("SiAPITXGAIN wouldn't return nice things\n");
		return -1;
	}
	
	granita0_tx_freqsel(dgb_dt, freq_mhz);
    //granita0_tx_power(dgb_dt, freq_mhz, tx_ch->power);
	granita0_tx_power(dgb_dt, freq_mhz, 0);
	
	if(tx_ch->path == RFNM_PATH_SMA_A) {
		granita0_ant_a_tx(dgb_dt);
	} else if(tx_ch->path == RFNM_PATH_SMA_B) {
		granita0_ant_b_tx(dgb_dt);
	}

	rfnm_fe_load_latches(dgb_dt);
	rfnm_fe_trigger_latches(dgb_dt);

	return 0;
fail: 
	return -EAGAIN;
}

int rfnm_rx_ch_set(struct rfnm_dgb *dgb_dt, struct rfnm_api_rx_ch * rx_ch) {
	
	int freq = rx_ch->freq / (1000 * 1000);
	int dbm = rx_ch->gain;
	int ret;

	if(rx_ch->dgb_ch_id == 0) {
		if(rx_ch->path == RFNM_PATH_SMA_A) {
			if(dbm < -12) {
				// enable 24 dB attenuator
				dbm += 24;
				granita0_ant_a_attn_24(dgb_dt);
			} else if(dbm < 0) {
				// enable 12 dB attenuator
				dbm += 12;
				granita0_ant_a_attn_12(dgb_dt);
			} else {
				granita0_ant_a_through(dgb_dt);
			}
		}

		if(rx_ch->path == RFNM_PATH_SMA_B) {
			granita0_tx_power(dgb_dt, -100, 0);
			granita0_ant_a_crossover(dgb_dt);
		}

		if(rx_ch->path == RFNM_PATH_EMBED_ANT) {
			granita0_tx_power(dgb_dt, freq, -100);
			if(freq < 3000) {
				granita0_ant_a_embeded_lf(dgb_dt);
			} else {
				granita0_ant_a_embeded_hf(dgb_dt);
			}
		}

		if(dbm > 0) {
			if(dbm > 64) {
				dbm = 64;
			}
			ret = SiAPIRXGAIN(SiCoreChar[dgb_dt->dgb_id], 2, dbm);
			if(ret) {
				printk("SiAPIRXGAIN failed\n");
				goto fail;
			}
		}

		granita0_fa(dgb_dt, freq);
	}
	
	if(rx_ch->dgb_ch_id == 1) {
		if(rx_ch->path == RFNM_PATH_SMA_B) {
			if(dbm < -12) {
				// enable 24 dB attenuator
				dbm += 24;
				granita0_ant_b_attn_24(dgb_dt);
			} else if(dbm < 0) {
				// enable 12 dB attenuator
				dbm += 12;
				granita0_ant_b_attn_12(dgb_dt);
			} else {
				granita0_ant_b_through(dgb_dt);
			}
		}

		if(rx_ch->path == RFNM_PATH_SMA_A) {
			granita0_tx_power(dgb_dt, freq, -100);
			granita0_ant_b_crossover(dgb_dt);
			ret = SiAPIRXGAIN(SiCoreChar[dgb_dt->dgb_id], 2, dbm);
		}


		if(rx_ch->path == RFNM_PATH_EMBED_ANT) {
			granita0_tx_power(dgb_dt, freq, -100);
			if(freq < 3000) {
				granita0_ant_b_embeded_lf(dgb_dt);
			} else {
				granita0_ant_b_embeded_hf(dgb_dt);
			}
		}

		if(dbm > 0) {
			if(dbm > 64) {
				dbm = 64;
			}
			ret = SiAPIRXGAIN(SiCoreChar[dgb_dt->dgb_id], 1, dbm);
			if(ret) {
				printk("SiAPIRXGAIN failed\n");
				goto fail;
			}
		}
		
		granita0_fb(dgb_dt, freq);
	}

	

	rfnm_fe_load_latches(dgb_dt);
	rfnm_fe_trigger_latches(dgb_dt);

	return 0;

	
fail: 
	return -EAGAIN;
}


static int rfnm_granita_probe(struct spi_device *spi)
{
	struct rfnm_bootconfig *cfg;
	struct rfnm_eeprom_data *eeprom_data;
	cfg = memremap(RFNM_BOOTCONFIG_PHYADDR, SZ_4M, MEMREMAP_WB);

	struct spi_master *spi_master;
	spi_master = spi->master;
	int dgb_id = spi_master->bus_num - 1;

	if(	cfg->daughterboard_present[dgb_id] != RFNM_DAUGHTERBOARD_PRESENT ||
		cfg->daughterboard_eeprom[dgb_id].board_id != RFNM_DAUGHTERBOARD_GRANITA) {
		//printk("RFNM: Granita driver loaded, but daughterboard is not Granita\n");
		memunmap(cfg);
		return -ENODEV;
	}

	printk("RFNM: Loading Granita driver for daughterboard at slot %d\n", dgb_id);

	rfnm_gpio_output(dgb_id, RFNM_DGB_GPIO4_7); // granita power
	rfnm_gpio_output(dgb_id, RFNM_DGB_GPIO4_6); // granita nrst

	rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_7); 
	rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_6); 

	msleep(1);

	rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_7); 
	rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_6); 

//# gpioset 2 21=0 // rffc NRST

	struct device *dev = &spi->dev;
	struct rfnm_dgb *dgb_dt;
	
	const struct spi_device_id *id = spi_get_device_id(spi);
	int i, ret;

	dgb_dt = devm_kzalloc(dev, sizeof(*dgb_dt), GFP_KERNEL);
	if(!dgb_dt) {
		return -ENOMEM;
	}

	dgb_dt->dgb_id = dgb_id;

	dgb_dt->rx_ch_set = rfnm_rx_ch_set;
	dgb_dt->rx_ch_get = rfnm_rx_ch_get;
	dgb_dt->tx_ch_set = rfnm_tx_ch_set;
	dgb_dt->tx_ch_get = rfnm_tx_ch_get;

	spi_set_drvdata(spi, dgb_dt);

	spi->max_speed_hz = 1000000;
	spi->bits_per_word = 8;
	spi->mode = SPI_CPHA;

	rfnm_fe_generic_init(dgb_dt, RFNM_GRANITA0_NUM_LATCHES); 


    dgb_dt->priv_drv = kmalloc (100, GFP_KERNEL );

    ret = SiAPIOpen(SiCoreChar[dgb_dt->dgb_id], dgb_dt->priv_drv, spi);
    if(ret) {
        printk("Si core wouldn't open\n");
        return -1;
    }

	ret = SiAPIPowerUpRX(SiCoreChar[dgb_dt->dgb_id], 2, 1850 * 1000, 100000);
	if(ret) {
		return -1;
		//printf("SiAPIPowerUpRX wouldn't return nice things\n");
	}

	printk("RFNM: Granita daughterboard initialized\n");

	//rfnm_daughterboard_register_cb(slot, RFNM_CB_RX_TUNE);
	


	
	struct rfnm_api_tx_ch *tx_ch, *tx_s;
	struct rfnm_api_rx_ch *rx_ch[2], *rx_s[2];

	tx_ch = devm_kzalloc(dev, sizeof(*tx_ch), GFP_KERNEL);
	rx_ch[0] = devm_kzalloc(dev, sizeof(*rx_ch), GFP_KERNEL);
	rx_ch[1] = devm_kzalloc(dev, sizeof(*rx_ch), GFP_KERNEL);
	tx_s = devm_kzalloc(dev, sizeof(*tx_ch), GFP_KERNEL);
	rx_s[0] = devm_kzalloc(dev, sizeof(*rx_ch), GFP_KERNEL);
	rx_s[1] = devm_kzalloc(dev, sizeof(*rx_ch), GFP_KERNEL);
	if(!tx_ch || !rx_ch[0] || !rx_ch[1] || !tx_s || !rx_s[0] || !rx_s[1]) {
		return -ENOMEM;
	}

	tx_ch->freq_max = 6300 MHZ_TO_HZ;
	tx_ch->freq_min = 600 MHZ_TO_HZ;
	rfnm_dgb_reg_tx_ch(dgb_dt, tx_ch, tx_s);

	rx_ch[0]->freq_max = 6300 MHZ_TO_HZ;
	rx_ch[0]->freq_min = 600 MHZ_TO_HZ;
	rfnm_dgb_reg_rx_ch(dgb_dt, rx_ch[0], rx_s[0]);
	
	rx_ch[1]->freq_max = 6300 MHZ_TO_HZ;
	rx_ch[1]->freq_min = 600 MHZ_TO_HZ;
	rfnm_dgb_reg_rx_ch(dgb_dt, rx_ch[1], rx_s[1]);

/*
		cfg->daughterboard_eeprom[dgb_id].board_id, 
		cfg->daughterboard_eeprom[dgb_id].board_revision_id, 
		cfg->daughterboard_eeprom[dgb_id].serial_number*/
	rfnm_dgb_reg(dgb_dt);

	return 0;
}

static int rfnm_granita_remove(struct spi_device *spi)
{
	struct rfnm_dgb *dgb_dt;
	dgb_dt = spi_get_drvdata(spi);

	//struct spi_controller *ctlr;

	//ctlr = dev_get_drvdata(&pdev->dev);
	//spi_unregister_controller(ctlr);
	rfnm_dgb_unreg(dgb_dt); 
	return 0;
}

static const struct spi_device_id rfnm_granita_ids[] = {
	{ "rfnm,daughterboard" },
	{},
};
MODULE_DEVICE_TABLE(spi, rfnm_granita_ids);


static const struct of_device_id rfnm_granita_match[] = {
	{ .compatible = "rfnm,daughterboard" },
	{},
};
MODULE_DEVICE_TABLE(of, rfnm_granita_match);

static struct spi_driver rfnm_granita_spi_driver = {
	.driver = {
		.name = "rfnm_granita",
		.of_match_table = rfnm_granita_match,
	},
	.probe = rfnm_granita_probe,
	.remove = rfnm_granita_remove,
	.id_table = rfnm_granita_ids,
};

module_spi_driver(rfnm_granita_spi_driver);


MODULE_PARM_DESC(device, "RFNM Granita Daughterboard Driver");

MODULE_LICENSE("GPL");