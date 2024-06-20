#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/dma-mapping.h>
#include <la9310_base.h>
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

#include "limesuiteng/embedded/lms7002m/lms7002m.h"
#include "limesuiteng/embedded/loglevel.h"

#include "rfnm_lime0_regs.h"

#define LMS_REF_FREQ (51200000)

#include <linux/rfnm-shared.h>
#include <linux/rfnm-gpio.h>

#include "rfnm_fe_generic.h"
#include "rfnm_fe_lime0.h"


static int lms_spi16_transact(const uint32_t* mosi, uint32_t* miso, uint32_t count, void* userData) {
	struct spi_device *spi = (struct spi_device *)userData;
	struct spi_transfer xfer;
	uint32_t txbuf;
	uint32_t rxbuf;
	size_t i;
	int ret;

	for (i=0; i < count; i++) {
		memset(&xfer, 0, sizeof(xfer));
		txbuf = mosi[i];
		xfer.tx_buf = &txbuf;
		xfer.len = 4;
		if (!(txbuf & (1 << 31)) && miso)
			xfer.rx_buf = &rxbuf;

		ret = spi_sync_transfer(spi, &xfer, 1);

		if (ret < 0) {
			printk("spi_sync_transfer failed: %d\n", ret);
		}

		// if this is a read, signified by the most significant bit being cleared,
		// we need the data in the received buffer. If the miso output buffer is
		// not NULL, stick the data in there.
		if (!(txbuf & (1 << 31)) && miso) {
			miso[i] = rxbuf;
		}
	}

	return 0;
}

int parse_lime_iq_lpf(int mhz) {
	if(mhz >= 100) {
		return 100;
	} else if(mhz >= 90) {
		return 90;
	} else if(mhz >= 80) {
		return 80;
	} else if(mhz >= 70) {
		return 70;
	} else if(mhz >= 60) {
		return 60;
	} else if(mhz >= 50) {
		return 50;
	} else if(mhz >= 40) {
		return 40;
	} else if(mhz >= 30) {
		return 30;
	} else if(mhz >= 20) {
		return 20;
	} else if(mhz >= 15) {
		return 15;
	} else if(mhz >= 10) {
		return 10;
	} else if(mhz >= 5) {
		return 5;
	} else if(mhz >= 3) {
		return 3;
	} else if(mhz >= 1) {
		return 1;
	} else {
		return 100;
	}
}

void lime0_set_iq_tx_lpf_bandwidth(struct rfnm_dgb *dgb_dt, int txbw) {
	struct lms7002m_context *lms;
	lms = dgb_dt->priv_drv;

	lms7002m_set_active_channel(lms, LMS7002M_CHANNEL_A);

	// LMS7002M_set_mac_ch(lms, LMS_CHA);
	switch(txbw) {
		case 100:
			// lms7002m_spi_modify_csr(lms, LMS7002M_CG_IAMP_TBB, 9);
			// lms->regs->reg_0x0108_cg_iamp_tbb = 9;
			// lms->regs->reg_0x0109_rcal_lpfh_tbb = 52;
			// lms->regs->reg_0x0109_rcal_lpflad_tbb = 193;
			// lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			// lms->regs->reg_0x010a_ccal_lpflad_tbb = 25;
			break;
		case 90:
			// lms->regs->reg_0x0108_cg_iamp_tbb = 7;
			// lms->regs->reg_0x0109_rcal_lpfh_tbb = 40;
			// lms->regs->reg_0x0109_rcal_lpflad_tbb = 193;
			// lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			// lms->regs->reg_0x010a_ccal_lpflad_tbb = 25;
			break;

		case 80:
			// lms->regs->reg_0x0108_cg_iamp_tbb = 7;
			// lms->regs->reg_0x0109_rcal_lpfh_tbb = 29;
			// lms->regs->reg_0x0109_rcal_lpflad_tbb = 193;
			// lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			// lms->regs->reg_0x010a_ccal_lpflad_tbb = 25;
			break;

		case 70:
			// lms->regs->reg_0x0108_cg_iamp_tbb = 6;
			// lms->regs->reg_0x0109_rcal_lpfh_tbb = 18;
			// lms->regs->reg_0x0109_rcal_lpflad_tbb = 193;
			// lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			// lms->regs->reg_0x010a_ccal_lpflad_tbb = 24;
			break;
		case 60:
			// lms->regs->reg_0x0108_cg_iamp_tbb = 5;
			// lms->regs->reg_0x0109_rcal_lpfh_tbb = 7;
			// lms->regs->reg_0x0109_rcal_lpflad_tbb = 193;
			// lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			// lms->regs->reg_0x010a_ccal_lpflad_tbb = 24;
			break;
		case 50:
			// lms->regs->reg_0x0108_cg_iamp_tbb = 4;
			// lms->regs->reg_0x0109_rcal_lpfh_tbb = 0;
			// lms->regs->reg_0x0109_rcal_lpflad_tbb = 193;
			// lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			// lms->regs->reg_0x010a_ccal_lpflad_tbb = 29;
			break;
		case 40:
			// lms->regs->reg_0x0108_cg_iamp_tbb = 33;
			// lms->regs->reg_0x0109_rcal_lpfh_tbb = 97;
			// lms->regs->reg_0x0109_rcal_lpflad_tbb = 255;
			// lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			// lms->regs->reg_0x010a_ccal_lpflad_tbb = 20;
			break;
		case 30:
			// lms->regs->reg_0x0108_cg_iamp_tbb = 34;
			// lms->regs->reg_0x0109_rcal_lpfh_tbb = 97;
			// lms->regs->reg_0x0109_rcal_lpflad_tbb = 204;
			// lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			// lms->regs->reg_0x010a_ccal_lpflad_tbb = 26;
			break;
		case 20:
			// lms->regs->reg_0x0108_cg_iamp_tbb = 33;
			// lms->regs->reg_0x0109_rcal_lpfh_tbb = 97;
			// lms->regs->reg_0x0109_rcal_lpflad_tbb = 120;
			// lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			// lms->regs->reg_0x010a_ccal_lpflad_tbb = 25;
			break;
		case 15:
			// lms->regs->reg_0x0108_cg_iamp_tbb = 22;
			// lms->regs->reg_0x0109_rcal_lpfh_tbb = 97;
			// lms->regs->reg_0x0109_rcal_lpflad_tbb = 77;
			// lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			// lms->regs->reg_0x010a_ccal_lpflad_tbb = 24;
			break;
		case 10:
			// lms->regs->reg_0x0108_cg_iamp_tbb = 22;
			// lms->regs->reg_0x0109_rcal_lpfh_tbb = 97;
			// lms->regs->reg_0x0109_rcal_lpflad_tbb = 35;
			// lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			// lms->regs->reg_0x010a_ccal_lpflad_tbb = 24;
			break;
		case 5:
			// lms->regs->reg_0x0108_cg_iamp_tbb = 22;
			// lms->regs->reg_0x0109_rcal_lpfh_tbb = 97;
			// lms->regs->reg_0x0109_rcal_lpflad_tbb = 0;
			// lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			// lms->regs->reg_0x010a_ccal_lpflad_tbb = 31;
			break;
	}

	// LMS7002M_regs_spi_write(lms, 0x0108);
	// LMS7002M_regs_spi_write(lms, 0x0109);
	// LMS7002M_regs_spi_write(lms, 0x010a);

	// lms->regs->reg_0x010b_value = 1;
	// LMS7002M_regs_spi_write(lms, 0x010b);

	// if(txbw >= 50) {
	// 	LMS7002M_tbb_set_path(lms, LMS_CHA, LMS7002M_TBB_HBF);
	// 	LMS7002M_tbb_set_test_in(lms, LMS_CHA, LMS7002M_TBB_TSTIN_LBF);
	// } else {
	// 	LMS7002M_tbb_set_path(lms, LMS_CHA, LMS7002M_TBB_LBF);
	// 	LMS7002M_tbb_set_test_in(lms, LMS_CHA, LMS7002M_TBB_TSTIN_HBF);
	// }
}

void rfnm_lime_set_bias_t(struct rfnm_dgb *dgb_dt, enum rfnm_bias_tee bias_tee) {
	if(bias_tee == RFNM_BIAS_TEE_ON) {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_ANT_BIAS, 0);
	} else {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_ANT_BIAS, 1);
	}
}

void lime0_set_rx_gain(struct rfnm_dgb *dgb_dt, struct rfnm_api_rx_ch * rx_ch) {
	struct lms7002m_context *lms;
	lms = dgb_dt->priv_drv;
	int dbm = rx_ch->gain;

	if(rx_ch->path == RFNM_PATH_EMBED_ANT) {
		lime0_ant_embed(dgb_dt);
		//printk("embed\n");
	} else {
		//printk("not embed\n");
		if(dbm < -12) {
			// enable 24 dB attenuator
			dbm += 24;
			lime0_ant_attn_24(dgb_dt);
		} else if(dbm < 0) {
			// enable 12 dB attenuator
			dbm += 12;
			lime0_ant_attn_12(dgb_dt);
		} else {
			lime0_ant_through(dgb_dt);
		}
	}

	if(dbm >= 0) {
		if(dbm > 30) {
			dbm = 30;
		}
		lms7002m_set_rfelna_db(lms, dbm, LMS7002M_CHANNEL_AB);
	} else {
		lms7002m_set_rfelna_db(lms, 0, LMS7002M_CHANNEL_AB);
	}
}

void rfnm_tx_ch_get(struct rfnm_dgb *dgb_dt, struct rfnm_api_tx_ch * tx_ch) {
	printk("inside rfnm_tx_ch_get\n");
}
void rfnm_rx_ch_get(struct rfnm_dgb *dgb_dt, struct rfnm_api_rx_ch * rx_ch) {
	printk("inside rfnm_rx_ch_get\n");
}

int rfnm_tx_ch_set(struct rfnm_dgb *dgb_dt, struct rfnm_api_tx_ch * tx_ch) {
	rfnm_api_failcode ecode = RFNM_API_OK;
	lime_Result ret;
	uint64_t actualRate = 0;
	uint64_t freq = tx_ch->freq / (MHZ_TO_HZ(1));
	struct lms7002m_context *lms;
	lms = dgb_dt->priv_drv;

	lms7002m_set_active_channel(lms, LMS7002M_CHANNEL_A);

	// need to enable those modules before other writes
	lms7002m_enable_channel(lms, true, LMS7002M_CHANNEL_A, true);

	// this might fail because some tx paths are disabled when run in rx mode?
	ret = lms7002m_set_frequency_sx(lms, true, tx_ch->freq);
	if(ret) {
		printk("Tuning failed\n");
		ecode = RFNM_API_TUNE_FAIL;
		goto fail;
	}
	actualRate = lms7002m_get_frequency_sx(lms, true);
	printk("%d - Actual TX LO freq %llu Hz\n", ret, actualRate);

	if(freq < 1500) {
		lime0_tx_band(dgb_dt, RFNM_LIME0_TX_BAND_LOW);
		lms7002m_set_band_trf(lms, 2);
	} else {
		lime0_tx_band(dgb_dt, RFNM_LIME0_TX_BAND_HIGH);
		lms7002m_set_band_trf(lms, 1);
	}

	lime0_tx_power(dgb_dt, freq, tx_ch->power);
	lime0_tx_lpf(dgb_dt, freq);

	lime0_set_iq_tx_lpf_bandwidth(dgb_dt, parse_lime_iq_lpf(tx_ch->rfic_lpf_bw));
	// lms7002m_set_tx_lpf(lms, MHZ_TO_HZ(tx_ch->rfic_lpf_bw));

	if(tx_ch->path != RFNM_PATH_LOOPBACK) {
		lime0_ant_tx(dgb_dt);
		lime0_disable_all_lna(dgb_dt);
	} else {
		lime0_loopback(dgb_dt);
	}

	rfnm_lime_set_bias_t(dgb_dt, tx_ch->bias_tee);

	rfnm_fe_load_order(dgb_dt, RFNM_LO_LIME0_FA, RFNM_LO_LIME0_ANT, RFNM_LO_LIME0_TX, RFNM_LO_LIME0_END);

	rfnm_fe_load_latches(dgb_dt);
	rfnm_fe_trigger_latches(dgb_dt);

	if(tx_ch->enable == RFNM_CH_ON_TDD) {
		memcpy(&dgb_dt->fe_tdd[RFNM_TX], &dgb_dt->fe, sizeof(struct fe_s));
		if(dgb_dt->rx_s[0]->enable == RFNM_CH_ON_TDD) {
			rfnm_dgb_en_tdd(dgb_dt, tx_ch, dgb_dt->rx_s[0]);
		}
	}

	memcpy(dgb_dt->tx_s[0], dgb_dt->tx_ch[0], sizeof(struct rfnm_api_tx_ch));

	return 0;

fail:
	return -ecode;

}

int rfnm_rx_ch_set(struct rfnm_dgb *dgb_dt, struct rfnm_api_rx_ch * rx_ch) {
	lime_Result ret;
	rfnm_api_failcode ecode = RFNM_API_OK;
	uint64_t actualRate = 0;
	struct lms7002m_context *lms;
	lms = dgb_dt->priv_drv;
	uint64_t freq = rx_ch->freq / (MHZ_TO_HZ(1));

	if(rx_ch->fm_notch == RFNM_FM_NOTCH_AUTO) {
		lime0_fm_notch(dgb_dt, 1);
	} else if(rx_ch->fm_notch == RFNM_FM_NOTCH_ON) {
		lime0_fm_notch(dgb_dt, 1);
	} else if(rx_ch->fm_notch == RFNM_FM_NOTCH_OFF) {
		lime0_fm_notch(dgb_dt, 0);
	}


	if(freq <= 2) {
		lime0_filter_0_2(dgb_dt);
	} else if(freq <= 12) {
		lime0_filter_2_12(dgb_dt);
	} else if(freq <= 30) {
		lime0_filter_12_30(dgb_dt);
	} else if(freq <= 60) {
		lime0_filter_30_60(dgb_dt);
	} else if(freq <= 120) {
		lime0_filter_60_120(dgb_dt);
		if(rx_ch->fm_notch == RFNM_FM_NOTCH_AUTO) {
			lime0_fm_notch(dgb_dt, 0);
		}
	} else if(freq <= 250) {
		lime0_filter_120_250(dgb_dt);
		if(freq <= 150 && rx_ch->fm_notch == RFNM_FM_NOTCH_AUTO) {
			lime0_fm_notch(dgb_dt, 0);
		}
	} else if(freq <= 480) {
		lime0_filter_250_480(dgb_dt);
	} else if(freq <= 1000) {
		lime0_filter_480_1000(dgb_dt);
	} else {
		if(freq >= 1166 && freq <= 1229) {
			lime0_filter_1166_1229(dgb_dt);
		} else if(freq >= 1574 && freq <= 1605) {
			lime0_filter_1574_1605(dgb_dt);
		} else if(freq >= 1805 && freq <= 2250) { // 2250? test
			lime0_filter_1805_2200(dgb_dt);
		} else if(freq >= 2250 && freq <= 2700) {
			lime0_filter_2300_2690(dgb_dt);
		} else {
			lime0_filter_950_4000(dgb_dt);
		}
	}

	lms7002m_set_active_channel(lms, LMS7002M_CHANNEL_A);

	if(freq <= 60) {
		lms7002m_set_path_rfe(lms, LMS7002M_PATH_RFE_LNAL);
	} else if(freq <= 1400) {
		lms7002m_set_path_rfe(lms, LMS7002M_PATH_RFE_LNAW);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_LRIM, 0);
	} else {
		lms7002m_set_path_rfe(lms, LMS7002M_PATH_RFE_LNAH);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_LRIM, 1);
	}

	if(freq < 30) {
		freq = 30;
	}

	ret = lms7002m_set_frequency_sx(lms, false, rx_ch->freq);
	if(ret) {
		printk("Tuning failed!\n");
		ecode = RFNM_API_TUNE_FAIL;
		goto fail;
	}

	actualRate = lms7002m_get_frequency_sx(lms, false);
	printk("%d - Actual RX LO freq %llu Hz\n", ret, actualRate);

	lms7002m_set_rx_lpf(lms, MHZ_TO_HZ(rx_ch->rfic_lpf_bw));

	if(rx_ch->rfic_dc_q || rx_ch->rfic_dc_i) {
		int16_t q = rx_ch->rfic_dc_q;
		int16_t i = rx_ch->rfic_dc_i;

		if(q > 63 || q < -63 || i > 63 || i < -63) {
			printk("lms_set_dcoff out of range q [-63 63] i [-63 63]\n");
		}

		if(q < 0) {
			q = -q;
			q |= (1 << 6);
		}

		if(i < 0) {
			i = -i;
			i |= (1 << 6);
		}

		lms7002m_set_dc_offset(lms, false, i, q);
	}

	lime0_set_rx_gain(dgb_dt, rx_ch);

	if(rx_ch->path != RFNM_PATH_LOOPBACK) {
		lime0_tx_power(dgb_dt, 1000, -100);
		lime0_disable_all_pa(dgb_dt);
	} else {
		lime0_disable_all_pa(dgb_dt);
	}

	rfnm_lime_set_bias_t(dgb_dt, rx_ch->bias_tee);

	rfnm_fe_load_order(dgb_dt, RFNM_LO_LIME0_TX, RFNM_LO_LIME0_ANT, RFNM_LO_LIME0_FA, RFNM_LO_LIME0_END);

	rfnm_fe_load_latches(dgb_dt);
	rfnm_fe_trigger_latches(dgb_dt);


	if(rx_ch->enable == RFNM_CH_ON_TDD) {
		memcpy(&dgb_dt->fe_tdd[RFNM_RX], &dgb_dt->fe, sizeof(struct fe_s));
		if(dgb_dt->tx_s[0]->enable == RFNM_CH_ON_TDD) {
			rfnm_dgb_en_tdd(dgb_dt, dgb_dt->tx_s[0], rx_ch);
		}
	}

	memcpy(dgb_dt->rx_s[0], dgb_dt->rx_ch[0], sizeof(struct rfnm_api_rx_ch));

	return 0;

fail:
	return -ecode;
}

static void rfnm_lime_log_callback(int level, const char* message, void* handle) {
	(void) handle;

	if(level <= lime_LogLevel_Error) {
		printk(message);
	}
	
}

static void rfnm_lime_cgen_frequency_changed(void* handle) {
	(void) handle;
	return;
}

static int rfnm_lime_probe(struct spi_device *spi)
{
	struct rfnm_bootconfig *cfg;
	struct rfnm_eeprom_data *eeprom_data;
	cfg = memremap(RFNM_BOOTCONFIG_PHYADDR, SZ_4M, MEMREMAP_WB);

	struct spi_master *spi_master;
	spi_master = spi->master;
	int dgb_id = spi_master->bus_num - 1;

	if(	cfg->daughterboard_present[dgb_id] != RFNM_DAUGHTERBOARD_PRESENT ||
		cfg->daughterboard_eeprom[dgb_id].board_id != RFNM_DAUGHTERBOARD_LIME) {
		memunmap(cfg);
		return -ENODEV;
	}

	printk("RFNM: Loading Lime driver for daughterboard at slot %d\n", dgb_id);

	rfnm_gpio_output(dgb_id, RFNM_DGB_GPIO3_22); // 1.4V LMS
	rfnm_gpio_output(dgb_id, RFNM_DGB_GPIO4_8); // 1.25V LMS

	rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO3_22); // 1.4V LMS
	rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_8); // 1.25V LMS

 	struct device *dev = &spi->dev;
	struct rfnm_dgb *dgb_dt;

	const struct spi_device_id *id = spi_get_device_id(spi);
	int i, ret;

	dgb_dt = devm_kzalloc(dev, sizeof(struct rfnm_dgb), GFP_KERNEL);
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
	spi->bits_per_word = 32;
	spi->mode = 0;

	rfnm_fe_generic_init(dgb_dt, RFNM_LIME0_NUM_LATCHES);

	lms7002m_hooks lms_hooks;

	lms_hooks.log = rfnm_lime_log_callback;
	lms_hooks.spi16_transact = lms_spi16_transact;
	lms_hooks.spi16_userData = spi;
	lms_hooks.on_cgen_frequency_changed = rfnm_lime_cgen_frequency_changed;

	struct lms7002m_context *lms;
	lms = lms7002m_create(&lms_hooks);
	if (lms == NULL) {
		return -1;
	}
	dgb_dt->priv_drv = lms;

	const uint32_t reset_cmd = ((0x8000 | 0x0020) << 16) | 0x0000;
	int spi_result = spi_write(spi, &reset_cmd, 4);
	const uint32_t release_reset_cmd = ((0x8000 | 0x0020) << 16) | 0xFFFD;
	spi_result = spi_write(spi, &release_reset_cmd, 4);
	// Both the new and old LimeSuite drivers do this on a reset. ??
	const uint32_t write_mimo_ch_b_cmd = ((0x8000 | 0x002E) << 16) | 0x0000;
	spi_result = spi_write(spi, &write_mimo_ch_b_cmd, 4);

	// set 4-wire spi before reading back
	// TODO: is this really necessary?
	const uint32_t set_4wire_spi_cmd = ((0x8000 | 0x0021) << 16) | 0x0E9F;
	spi_result = spi_write(spi, &set_4wire_spi_cmd, 4);

	//read info register
	const uint32_t read_lms_info_reg_cmd = 0x002f << 16;
	uint32_t read_lms_info_reg_value;
	spi_result = lms_spi16_transact(&read_lms_info_reg_cmd, &read_lms_info_reg_value, 1, spi);
	printk("ver 0x%x, rev 0x%x, mask 0x%x\n",
		(read_lms_info_reg_value >> 11) & 0x1f,
		(read_lms_info_reg_value >> 6) & 0x1f,
		(read_lms_info_reg_value >> 0) & 0x3f);

	if(!((read_lms_info_reg_value >> 6) & 0x1f)) {
		printk("LMS Not Found!");
		lms7002m_destroy(lms);
		return -1;
	}

	// An attempt at doing a bulk write of all initial registers was not
	// successful, despite the LMS7002M datasheet saying that: "multiple
	// read/write is possible by repeating the instruction/data sequence while
	// keeping SEN low". As such, we'll do individual SPI transfers for now. It
	// seems the SEN toggle between some or all writes is important.
	for(i = 0; i < RFNM_LMS_REGS_CNT_ALL; i++) {
		uint32_t spi_cmd = ((0x8000 | rfnm_lms_init_regs_all[i][0]) << 16) | rfnm_lms_init_regs_all[i][1];
		spi_write(spi, &spi_cmd, 4);
	}

	//turn the clocks on
	lime_Result lms_ret = lms7002m_set_reference_clock(lms, LMS_REF_FREQ);
	if (ret != 0) {
		printk("provided reference clock invalid %d\n", ret);
		return -1;
	}

	// rx path is always enabled
	lms7002m_enable_channel(lms, false, LMS7002M_CHANNEL_A, true);

	// TODO: do we need to do something like this still?
	// tune tx PLL to a high frequency out of the way to avoid spurs... meh.
	lms7002m_set_frequency_sx(lms, true, MHZ_TO_HZ(3000));
	// then disable some of it
	lms7002m_enable_channel(lms, true, LMS7002M_CHANNEL_A, false);


	//lime0_tx_power(dgb_dt, 1000, 100000);
//	lime0_disable_all_pa(dgb_dt);
/*
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL1O, 0);



	rfnm_fe_load_latches(dgb_dt);
	rfnm_fe_trigger_latches(dgb_dt);


	rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL1O, 1);

	rfnm_fe_load_latches(dgb_dt);
	rfnm_fe_trigger_latches(dgb_dt);
*/

	rfnm_lime_set_bias_t(dgb_dt, RFNM_BIAS_TEE_OFF);
	rfnm_fe_load_latches(dgb_dt);
	rfnm_fe_trigger_latches(dgb_dt);

	printk("RFNM: Lime daughterboard initialized\n");

	struct rfnm_api_tx_ch *tx_ch, *tx_s;
	struct rfnm_api_rx_ch *rx_ch, *rx_s;

	if (dgb_id == 0) {
		tx_ch = devm_kzalloc(dev, sizeof(struct rfnm_api_tx_ch), GFP_KERNEL);
		tx_s = devm_kzalloc(dev, sizeof(struct rfnm_api_tx_ch), GFP_KERNEL);
		if (!tx_ch || !tx_s) {
			return -ENOMEM;
		}
	}
	rx_ch = devm_kzalloc(dev, sizeof(struct rfnm_api_rx_ch), GFP_KERNEL);
	rx_s = devm_kzalloc(dev, sizeof(struct rfnm_api_rx_ch), GFP_KERNEL);

	if(!rx_ch || !rx_s) {
		return -ENOMEM;
	}

	if (dgb_id == 0) {
		tx_ch->freq_max = MHZ_TO_HZ(3500);
		tx_ch->freq_min = MHZ_TO_HZ(10);
		tx_ch->path_preferred = RFNM_PATH_SMA_A;
		tx_ch->path_possible[0] = RFNM_PATH_SMA_A;
		tx_ch->path_possible[1] = RFNM_PATH_NULL;
		tx_ch->power_range.min = -60;
		tx_ch->power_range.max = 30;
		tx_ch->dac_id = 0;
		rfnm_dgb_reg_tx_ch(dgb_dt, tx_ch, tx_s);
	}

	rx_ch->freq_max = MHZ_TO_HZ(3500);
	rx_ch->freq_min = MHZ_TO_HZ(10);
	rx_ch->path_preferred = RFNM_PATH_SMA_A;
	rx_ch->path_possible[0] = RFNM_PATH_SMA_A;
	rx_ch->path_possible[1] = RFNM_PATH_EMBED_ANT;
	rx_ch->path_possible[2] = RFNM_PATH_NULL;
	rx_ch->gain_range.min = -24;
	rx_ch->gain_range.max = 30;
	rx_ch->adc_id = 0;
	rfnm_dgb_reg_rx_ch(dgb_dt, rx_ch, rx_s);

	dgb_dt->dac_ifs = 0x7;
	dgb_dt->dac_iqswap[0] = 0;
	dgb_dt->dac_iqswap[1] = 0;
	dgb_dt->adc_iqswap[0] = 1;
	dgb_dt->adc_iqswap[1] = 0;
	rfnm_dgb_reg(dgb_dt);

	return 0;
}

static int rfnm_lime_remove(struct spi_device *spi)
{
	struct rfnm_dgb *dgb_dt;
	dgb_dt = spi_get_drvdata(spi);

	lms7002m_destroy(dgb_dt->priv_drv);

	rfnm_dgb_unreg(dgb_dt);

	rfnm_gpio_clear(dgb_dt->dgb_id, RFNM_DGB_GPIO3_22); // 1.4V LMS
	rfnm_gpio_clear(dgb_dt->dgb_id, RFNM_DGB_GPIO4_8); // 1.25V LMS
	return 0;
}

static const struct spi_device_id rfnm_lime_ids[] = {
	{ "rfnm,daughterboard" },
	{},
};
MODULE_DEVICE_TABLE(spi, rfnm_lime_ids);


static const struct of_device_id rfnm_lime_match[] = {
	{ .compatible = "rfnm,daughterboard" },
	{},
};
MODULE_DEVICE_TABLE(of, rfnm_lime_match);

static struct spi_driver rfnm_lime_spi_driver = {
	.driver = {
		.name = "rfnm_lime",
		.of_match_table = rfnm_lime_match,
	},
	.probe = rfnm_lime_probe,
	.remove = rfnm_lime_remove,
	.id_table = rfnm_lime_ids,
};

module_spi_driver(rfnm_lime_spi_driver);


MODULE_PARM_DESC(device, "RFNM Lime Daughterboard Driver");

MODULE_LICENSE("GPL");
