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

#include <linux/rfnm-shared.h>
#include <linux/rfnm-gpio.h>

#include "rfnm_fe_generic.h"

#include "dtoa.h"




void rfnm_tx_ch_get(struct rfnm_dgb *dgb_dt, struct rfnm_api_tx_ch * tx_ch) {
	printk("inside rfnm_tx_ch_get\n");
}
void rfnm_rx_ch_get(struct rfnm_dgb *dgb_dt, struct rfnm_api_rx_ch * rx_ch) {
	printk("inside rfnm_rx_ch_get\n");
}



int rfnm_tx_ch_set(struct rfnm_dgb *dgb_dt, struct rfnm_api_tx_ch * tx_ch) {
	rfnm_api_failcode ecode = RFNM_API_OK;
	return -ecode;
}

int rfnm_rx_ch_set(struct rfnm_dgb *dgb_dt, struct rfnm_api_rx_ch * rx_ch) {
	rfnm_api_failcode ecode = RFNM_API_OK;
	return -ecode;
}



static int rfnm_breakout_probe(struct spi_device *spi)
{
	struct rfnm_bootconfig *cfg;
	struct rfnm_eeprom_data *eeprom_data;
	cfg = memremap(RFNM_BOOTCONFIG_PHYADDR, SZ_4M, MEMREMAP_WB);

	struct spi_master *spi_master;
	spi_master = spi->master;
	int dgb_id = spi_master->bus_num - 1;

	if(	cfg->daughterboard_present[dgb_id] != RFNM_DAUGHTERBOARD_PRESENT ||
		cfg->daughterboard_eeprom[dgb_id].board_id != RFNM_DAUGHTERBOARD_BREAKOUT) {
		memunmap(cfg);
		return -ENODEV;
	}

	printk("RFNM: Loading Breakout (dummy) driver for daughterboard at slot %d\n", dgb_id);


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

    dgb_dt->priv_drv = kmalloc (100, GFP_KERNEL );


	struct rfnm_api_tx_ch *tx_ch, *tx_s;
	struct rfnm_api_rx_ch *rx_ch[2], *rx_s[2];

	tx_ch = devm_kzalloc(dev, sizeof(struct rfnm_api_tx_ch), GFP_KERNEL);
	rx_ch[0] = devm_kzalloc(dev, sizeof(struct rfnm_api_rx_ch), GFP_KERNEL);
	rx_ch[1] = devm_kzalloc(dev, sizeof(struct rfnm_api_rx_ch), GFP_KERNEL);
	tx_s = devm_kzalloc(dev, sizeof(struct rfnm_api_tx_ch), GFP_KERNEL);
	rx_s[0] = devm_kzalloc(dev, sizeof(struct rfnm_api_rx_ch), GFP_KERNEL);
	rx_s[1] = devm_kzalloc(dev, sizeof(struct rfnm_api_rx_ch), GFP_KERNEL);
	
	if(!tx_ch || !rx_ch[0] || !rx_ch[1] || !tx_s || !rx_s[0] || !rx_s[1]) {
		return -ENOMEM;
	}

	tx_ch->freq_max = MHZ_TO_HZ(6300);
	tx_ch->freq_min = MHZ_TO_HZ(0);
	tx_ch->path_preferred = RFNM_PATH_SMA_B;
	tx_ch->dac_id = 0;
	rfnm_dgb_reg_tx_ch(dgb_dt, tx_ch, tx_s);

	rx_ch[0]->freq_max = MHZ_TO_HZ(6300);
	rx_ch[0]->freq_min = MHZ_TO_HZ(0);
	rx_ch[0]->path_preferred = RFNM_PATH_SMA_A;
	rx_ch[0]->adc_id = 0;
	rfnm_dgb_reg_rx_ch(dgb_dt, rx_ch[0], rx_s[0]);
	
	rx_ch[1]->freq_max = MHZ_TO_HZ(6300);
	rx_ch[1]->freq_min = MHZ_TO_HZ(0);
	rx_ch[1]->path_preferred = RFNM_PATH_SMA_B;
	rx_ch[1]->adc_id = 1;
	rfnm_dgb_reg_rx_ch(dgb_dt, rx_ch[1], rx_s[1]);

	dgb_dt->dac_ifs = 0xf;
	dgb_dt->dac_iqswap[0] = 0;
	dgb_dt->dac_iqswap[1] = 0;
	dgb_dt->adc_iqswap[0] = 0;
	dgb_dt->adc_iqswap[1] = 0;
	rfnm_dgb_reg(dgb_dt);
	
	return 0;
}

static int rfnm_breakout_remove(struct spi_device *spi)
{

	struct rfnm_dgb *dgb_dt;
	dgb_dt = spi_get_drvdata(spi);
	rfnm_dgb_unreg(dgb_dt); 
	return 0;
}

static const struct spi_device_id rfnm_breakout_ids[] = {
	{ "rfnm,daughterboard" },
	{},
};
MODULE_DEVICE_TABLE(spi, rfnm_breakout_ids);


static const struct of_device_id rfnm_breakout_match[] = {
	{ .compatible = "rfnm,daughterboard" },
	{},
};
MODULE_DEVICE_TABLE(of, rfnm_breakout_match);

static struct spi_driver rfnm_breakout_spi_driver = {
	.driver = {
		.name = "rfnm_breakout",
		.of_match_table = rfnm_breakout_match,
	},
	.probe = rfnm_breakout_probe,
	.remove = rfnm_breakout_remove,
	.id_table = rfnm_breakout_ids,
};

module_spi_driver(rfnm_breakout_spi_driver);


MODULE_PARM_DESC(device, "RFNM Breakout Daughterboard Driver");

MODULE_LICENSE("GPL");
