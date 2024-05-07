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

void kernel_neon_begin(void);
void kernel_neon_end(void);

#include <LMS7002M/LMS7002M.h>
#include <LMS7002M/LMS7002M_logger.h>
//#include <LMS7002M/LMS7002M_impl.h>
#include "LMS7002M_impl.h"
//#include "/home/davide/LMS7002M-driver/src/LMS7002M_impl.h"

#include "/home/davide/cli/lms_regs_init.h"

#define LMS_REF_FREQ (51.2e6)

#include <linux/rfnm-shared.h>
#include <linux/rfnm-gpio.h>

#include "rfnm_fe_generic.h"
#include "rfnm_fe_lime0.h"

#include "dtoa.h"


//void *lms_spi_handle;


/*struct rfnm_lms_spi {
	struct spi_controller *ctlr;
	void __iomem *base;
};*/


static inline uint32_t lms_interface_transact(void *handle, const uint32_t data, const bool readback) {
    struct spi_device *spi = (struct spi_device *)handle;
	int ret;

	kernel_neon_end();

    //transaction data structures
    struct spi_transfer xfer[1];
    unsigned char txbuf[4];
    unsigned char rxbuf[4];

    //setup transaction
    memset(xfer, 0, sizeof(xfer));
    xfer[0].tx_buf = (unsigned long)txbuf;
    //only specify rx buffer when readback is specified
    if (readback) {
		xfer[0].rx_buf = (unsigned long)rxbuf;
	}
    xfer[0].len = 4; //bytes

    //load tx data
    txbuf[3] = (data >> 24);
    txbuf[2] = (data >> 16);
    txbuf[1] = (data >> 8);
    txbuf[0] = (data >> 0);

	ret = spi_sync_transfer(spi, xfer, 1);
	if (ret < 0) {
		printk("spi_sync_transfer failed");
		//return ERR_SI_CORE_MAX_CNTWRTE;
	}

	kernel_neon_begin();

    //load rx data
    return \
        (((uint32_t)rxbuf[3]) << 24) |
        (((uint32_t)rxbuf[2]) << 16) |
        (((uint32_t)rxbuf[1]) << 8) |
        (((uint32_t)rxbuf[0]) << 0);
}

void lime0_set_iq_rx_lpf_bandwidth(struct rfnm_dgb *dgb_dt, int rxbw) {

	/*struct rfnm_dgb *dgb_dt;
	dgb_dt = spi_get_drvdata(spi);
	LMS7002M_t *lms;
    lms = dgb_dt->priv_drv;*/
	LMS7002M_t *lms;
    lms = dgb_dt->priv_drv;

	LMS7002M_set_mac_ch(lms, LMS_CHA);

	switch(rxbw) {
		case 100:
			lms->regs->reg_0x0116_c_ctl_lpfh_rbb = 45;
			lms->regs->reg_0x0117_c_ctl_lpfl_rbb = 12;
			lms->regs->reg_0x0116_rcc_ctl_lpfh_rbb = 0;
			lms->regs->reg_0x0117_rcc_ctl_lpfl_rbb = 5;
			break;
		case 90:
			lms->regs->reg_0x0116_c_ctl_lpfh_rbb = 45;
			lms->regs->reg_0x0117_c_ctl_lpfl_rbb = 12;
			lms->regs->reg_0x0116_rcc_ctl_lpfh_rbb = 3;
			lms->regs->reg_0x0117_rcc_ctl_lpfl_rbb = 5;
			break;
		case 80:
			lms->regs->reg_0x0116_c_ctl_lpfh_rbb = 74;
			lms->regs->reg_0x0117_c_ctl_lpfl_rbb = 12;
			lms->regs->reg_0x0116_rcc_ctl_lpfh_rbb = 2;
			lms->regs->reg_0x0117_rcc_ctl_lpfl_rbb = 5;
			break;
		case 70:
			lms->regs->reg_0x0116_c_ctl_lpfh_rbb = 94;
			lms->regs->reg_0x0117_c_ctl_lpfl_rbb = 12;
			lms->regs->reg_0x0116_rcc_ctl_lpfh_rbb = 1;
			lms->regs->reg_0x0117_rcc_ctl_lpfl_rbb = 5;
			break;
		case 60:
			lms->regs->reg_0x0116_c_ctl_lpfh_rbb = 126;
			lms->regs->reg_0x0117_c_ctl_lpfl_rbb = 12;
			lms->regs->reg_0x0116_rcc_ctl_lpfh_rbb = 0;
			lms->regs->reg_0x0117_rcc_ctl_lpfl_rbb = 5;
			break;
		case 50:
			lms->regs->reg_0x0116_c_ctl_lpfh_rbb = 159;
			lms->regs->reg_0x0117_c_ctl_lpfl_rbb = 12;
			lms->regs->reg_0x0116_rcc_ctl_lpfh_rbb = 0;
			lms->regs->reg_0x0117_rcc_ctl_lpfl_rbb = 5;
			break;
		case 40:
			lms->regs->reg_0x0116_c_ctl_lpfh_rbb = 217;
			lms->regs->reg_0x0117_c_ctl_lpfl_rbb = 12;
			lms->regs->reg_0x0116_rcc_ctl_lpfh_rbb = 0;
			lms->regs->reg_0x0117_rcc_ctl_lpfl_rbb = 5;
			break;
		case 30:
			lms->regs->reg_0x0116_c_ctl_lpfh_rbb = 128;
			lms->regs->reg_0x0117_c_ctl_lpfl_rbb = 10;
			lms->regs->reg_0x0116_rcc_ctl_lpfh_rbb = 1;
			lms->regs->reg_0x0117_rcc_ctl_lpfl_rbb = 4;
			break;
		case 20:
			lms->regs->reg_0x0116_c_ctl_lpfh_rbb = 128;
			lms->regs->reg_0x0117_c_ctl_lpfl_rbb = 66;
			lms->regs->reg_0x0116_rcc_ctl_lpfh_rbb = 1;
			lms->regs->reg_0x0117_rcc_ctl_lpfl_rbb = 3;
			break;
		case 15:
			lms->regs->reg_0x0116_c_ctl_lpfh_rbb = 128;
			lms->regs->reg_0x0117_c_ctl_lpfl_rbb = 123;
			lms->regs->reg_0x0116_rcc_ctl_lpfh_rbb = 1;
			lms->regs->reg_0x0117_rcc_ctl_lpfl_rbb = 3;
			break;
		case 10:
			lms->regs->reg_0x0116_c_ctl_lpfh_rbb = 128;
			lms->regs->reg_0x0117_c_ctl_lpfl_rbb = 236;
			lms->regs->reg_0x0116_rcc_ctl_lpfh_rbb = 1;
			lms->regs->reg_0x0117_rcc_ctl_lpfl_rbb = 3;
			break;
		case 5:
			lms->regs->reg_0x0116_c_ctl_lpfh_rbb = 128;
			lms->regs->reg_0x0117_c_ctl_lpfl_rbb = 566;
			lms->regs->reg_0x0116_rcc_ctl_lpfh_rbb = 1;
			lms->regs->reg_0x0117_rcc_ctl_lpfl_rbb = 2;
			break;
		case 3:
			lms->regs->reg_0x0116_c_ctl_lpfh_rbb = 128;
			lms->regs->reg_0x0117_c_ctl_lpfl_rbb = 724;
			lms->regs->reg_0x0116_rcc_ctl_lpfh_rbb = 1;
			lms->regs->reg_0x0117_rcc_ctl_lpfl_rbb = 1;
			break;
		case 1:
			lms->regs->reg_0x0116_c_ctl_lpfh_rbb = 128;
			lms->regs->reg_0x0117_c_ctl_lpfl_rbb = 2047;
			lms->regs->reg_0x0116_rcc_ctl_lpfh_rbb = 1;
			lms->regs->reg_0x0117_rcc_ctl_lpfl_rbb = 1;
			break;
	}

	LMS7002M_regs_spi_write(lms, 0x0117);
	LMS7002M_regs_spi_write(lms, 0x0116);

	if(rxbw > 100) {
		LMS7002M_rbb_set_path(lms, LMS_CHA, LMS7002M_RBB_BYP);
	} else if(rxbw >= 30) {
		LMS7002M_rbb_set_path(lms, LMS_CHA, LMS7002M_RBB_HBF);
	} else {
		LMS7002M_rbb_set_path(lms, LMS_CHA, LMS7002M_RBB_LBF);
	}
}

void lime0_set_iq_tx_lpf_bandwidth(struct rfnm_dgb *dgb_dt, int txbw) {

	/*struct rfnm_dgb *dgb_dt;
	dgb_dt = spi_get_drvdata(spi);
	LMS7002M_t *lms;
    lms = dgb_dt->priv_drv;*/
	LMS7002M_t *lms;
    lms = dgb_dt->priv_drv;
	

	LMS7002M_set_mac_ch(lms, LMS_CHA);
	switch(txbw) {
		case 100:
			lms->regs->reg_0x0108_cg_iamp_tbb = 9;
			lms->regs->reg_0x0109_rcal_lpfh_tbb = 52;
			lms->regs->reg_0x0109_rcal_lpflad_tbb = 193;
			lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			lms->regs->reg_0x010a_ccal_lpflad_tbb = 25;
			break;
		case 90:
			lms->regs->reg_0x0108_cg_iamp_tbb = 7;
			lms->regs->reg_0x0109_rcal_lpfh_tbb = 40;
			lms->regs->reg_0x0109_rcal_lpflad_tbb = 193;
			lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			lms->regs->reg_0x010a_ccal_lpflad_tbb = 25;
			break;

		case 80:
			lms->regs->reg_0x0108_cg_iamp_tbb = 7;
			lms->regs->reg_0x0109_rcal_lpfh_tbb = 29;
			lms->regs->reg_0x0109_rcal_lpflad_tbb = 193;
			lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			lms->regs->reg_0x010a_ccal_lpflad_tbb = 25;
			break;

		case 70:
			lms->regs->reg_0x0108_cg_iamp_tbb = 6;
			lms->regs->reg_0x0109_rcal_lpfh_tbb = 18;
			lms->regs->reg_0x0109_rcal_lpflad_tbb = 193;
			lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			lms->regs->reg_0x010a_ccal_lpflad_tbb = 24;
			break;
		case 60:
			lms->regs->reg_0x0108_cg_iamp_tbb = 5;
			lms->regs->reg_0x0109_rcal_lpfh_tbb = 7;
			lms->regs->reg_0x0109_rcal_lpflad_tbb = 193;
			lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			lms->regs->reg_0x010a_ccal_lpflad_tbb = 24;
			break;
		case 50:
			lms->regs->reg_0x0108_cg_iamp_tbb = 4;
			lms->regs->reg_0x0109_rcal_lpfh_tbb = 0;
			lms->regs->reg_0x0109_rcal_lpflad_tbb = 193;
			lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			lms->regs->reg_0x010a_ccal_lpflad_tbb = 29;
			break;
		case 40:
			lms->regs->reg_0x0108_cg_iamp_tbb = 33;
			lms->regs->reg_0x0109_rcal_lpfh_tbb = 97;
			lms->regs->reg_0x0109_rcal_lpflad_tbb = 255;
			lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			lms->regs->reg_0x010a_ccal_lpflad_tbb = 20;
			break;
		case 30:
			lms->regs->reg_0x0108_cg_iamp_tbb = 34;
			lms->regs->reg_0x0109_rcal_lpfh_tbb = 97;
			lms->regs->reg_0x0109_rcal_lpflad_tbb = 204;
			lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			lms->regs->reg_0x010a_ccal_lpflad_tbb = 26;
			break;
		case 20:
			lms->regs->reg_0x0108_cg_iamp_tbb = 33;
			lms->regs->reg_0x0109_rcal_lpfh_tbb = 97;
			lms->regs->reg_0x0109_rcal_lpflad_tbb = 120;
			lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			lms->regs->reg_0x010a_ccal_lpflad_tbb = 25;
			break;
		case 15:
			lms->regs->reg_0x0108_cg_iamp_tbb = 22;
			lms->regs->reg_0x0109_rcal_lpfh_tbb = 97;
			lms->regs->reg_0x0109_rcal_lpflad_tbb = 77;
			lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			lms->regs->reg_0x010a_ccal_lpflad_tbb = 24;
			break;
		case 10:
			lms->regs->reg_0x0108_cg_iamp_tbb = 22;
			lms->regs->reg_0x0109_rcal_lpfh_tbb = 97;
			lms->regs->reg_0x0109_rcal_lpflad_tbb = 35;
			lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			lms->regs->reg_0x010a_ccal_lpflad_tbb = 24;
			break;
		case 5:
			lms->regs->reg_0x0108_cg_iamp_tbb = 22;
			lms->regs->reg_0x0109_rcal_lpfh_tbb = 97;
			lms->regs->reg_0x0109_rcal_lpflad_tbb = 0;
			lms->regs->reg_0x010a_rcal_lpfs5_tbb = 76;
			lms->regs->reg_0x010a_ccal_lpflad_tbb = 31;
			break;
	}

	LMS7002M_regs_spi_write(lms, 0x0108);
	LMS7002M_regs_spi_write(lms, 0x0109);
	LMS7002M_regs_spi_write(lms, 0x010a);

	lms->regs->reg_0x010b_value = 1;
	LMS7002M_regs_spi_write(lms, 0x010b);     

	if(txbw >= 50) {
		LMS7002M_tbb_set_path(lms, LMS_CHA, LMS7002M_TBB_HBF);
		LMS7002M_tbb_set_test_in(lms, LMS_CHA, LMS7002M_TBB_TSTIN_LBF);
	} else {
		LMS7002M_tbb_set_path(lms, LMS_CHA, LMS7002M_TBB_LBF);
		LMS7002M_tbb_set_test_in(lms, LMS_CHA, LMS7002M_TBB_TSTIN_HBF);
	}
}

void lime0_set_rx_gain(struct rfnm_dgb *dgb_dt, int dbm) {

	LMS7002M_t *lms;
    lms = dgb_dt->priv_drv;

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

    //std::cout << "lms_gain_lna [0-30]" << endl;
    if(dbm > 0) {
        if(dbm > 30) {
            dbm = 30;
        }
        LMS7002M_rfe_set_lna(lms, LMS_CHAB, (float) dbm);
    }

    //std::cout << "lms_gain_tia [0-12]" << endl;
    //LMS7002M_rfe_set_tia(lms, LMS_CHAB, (float) gain);
    //std::cout << "lms_gain_pga [0-32]" << endl;
    //LMS7002M_rbb_set_pga(lms, LMS_CHAB, (float) gain);
}

void rfnm_tx_ch_get(struct rfnm_dgb *dgb_dt, struct rfnm_api_tx_ch * tx_ch) {
	printk("inside rfnm_tx_ch_get\n");
}
void rfnm_rx_ch_get(struct rfnm_dgb *dgb_dt, struct rfnm_api_rx_ch * rx_ch) {
	printk("inside rfnm_rx_ch_get\n");
}



int rfnm_tx_ch_set(struct rfnm_dgb *dgb_dt, struct rfnm_api_tx_ch * tx_ch) {
	kernel_neon_begin();
	rfnm_api_failcode ecode = RFNM_API_OK;
	int ret;
    double actualRate = 0.0;
	uint64_t freq = tx_ch->freq / (1000 * 1000);
	LMS7002M_t *lms;
    lms = dgb_dt->priv_drv;

	// need to enable those modules before other writes
	LMS7002M_sxx_enable(lms, LMS_TX, true);
	LMS7002M_tbb_enable(lms, LMS_CHA, true);
	LMS7002M_trf_enable(lms, LMS_CHA, true);

	// this might fail because some tx paths are disabled when run in rx mode?
	ret = LMS7002M_set_lo_freq(lms, LMS_TX, LMS_REF_FREQ, tx_ch->freq, &actualRate);
	if(ret) {
		printk("Tuning failed\n");
		ecode = RFNM_API_TUNE_FAIL;
		goto fail;
	}
	printk("%d - Actual TX LO freq %s MHz\n", ret, dtoa1(actualRate/1e6));

	if(freq < 1500) {
		lime0_tx_band(dgb_dt, RFNM_LIME0_TX_BAND_LOW);
		LMS7002M_trf_select_band(lms, LMS_CHA, 2);
	} else {
		lime0_tx_band(dgb_dt, RFNM_LIME0_TX_BAND_HIGH);
		LMS7002M_trf_select_band(lms, LMS_CHA, 1);
	}

	lime0_tx_power(dgb_dt, freq, tx_ch->power);
	lime0_tx_lpf(dgb_dt, freq);

	lime0_set_iq_tx_lpf_bandwidth(dgb_dt, tx_ch->iq_lpf_bw);

	if(tx_ch->path != RFNM_PATH_LOOPBACK) {
		lime0_ant_tx(dgb_dt);
		lime0_disable_all_lna(dgb_dt);
	} else {
		lime0_loopback(dgb_dt);
	}

	

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


	kernel_neon_end();

	return 0;

fail: 
	kernel_neon_end();
	return -ecode;

}

int rfnm_rx_ch_set(struct rfnm_dgb *dgb_dt, struct rfnm_api_rx_ch * rx_ch) {
	kernel_neon_begin();
	int ret;
	rfnm_api_failcode ecode = RFNM_API_OK;
    double actualRate = 0.0;
	LMS7002M_t *lms;
    lms = dgb_dt->priv_drv;
	uint64_t freq = rx_ch->freq / (1000 * 1000);

	
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
	
	

	if(freq <= 60) {
		LMS7002M_rfe_set_path(lms, LMS_CHA, LMS7002M_RFE_LNAL);
	} else if(freq <= 1400) {
		LMS7002M_rfe_set_path(lms, LMS_CHA, LMS7002M_RFE_LNAW);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_LRIM, 0);
	} else {
		LMS7002M_rfe_set_path(lms, LMS_CHA, LMS7002M_RFE_LNAH);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_LRIM, 1);
	}

	
	if(freq < 30) {
		freq = 30;
	}
	ret = LMS7002M_set_lo_freq(lms, LMS_RX, LMS_REF_FREQ, rx_ch->freq, &actualRate);

	if(ret) {
		printk("Tuning failed\n");
		ecode = RFNM_API_TUNE_FAIL;
		goto fail;
	}
	printk("%d - Actual RX LO freq %s MHz\n", ret, dtoa1(actualRate/1e6));

	

	lime0_set_iq_rx_lpf_bandwidth(dgb_dt, rx_ch->iq_lpf_bw);







	


	//LMS7002M_sxx_enable(lms, LMS_TX, false);
	//LMS7002M_tbb_enable(lms, LMS_CHA, false);
	//LMS7002M_trf_enable(lms, LMS_CHA, false);

	
	lime0_set_rx_gain(dgb_dt, rx_ch->gain);

	if(rx_ch->path != RFNM_PATH_LOOPBACK) {
		lime0_tx_power(dgb_dt, 1000, -100);
		lime0_disable_all_pa(dgb_dt);
	} else {
		lime0_disable_all_pa(dgb_dt);
	}

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


	kernel_neon_end();

	return 0;

fail: 
	kernel_neon_end();
	return -ecode;
}



static int rfnm_lime_probe(struct spi_device *spi)
{
	kernel_neon_begin();
	struct rfnm_bootconfig *cfg;
	struct rfnm_eeprom_data *eeprom_data;
	cfg = memremap(RFNM_BOOTCONFIG_PHYADDR, SZ_4M, MEMREMAP_WB);

	struct spi_master *spi_master;
	spi_master = spi->master;
	int dgb_id = spi_master->bus_num - 1;

	if(	cfg->daughterboard_present[dgb_id] != RFNM_DAUGHTERBOARD_PRESENT ||
		cfg->daughterboard_eeprom[dgb_id].board_id != RFNM_DAUGHTERBOARD_LIME) {
		//printk("RFNM: Lime driver loaded, but daughterboard is not Lime\n");
		memunmap(cfg);
		kernel_neon_end();
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
		kernel_neon_end();
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
	//spi->mode = SPI_CPHA;
	spi->mode = 0;


	rfnm_fe_generic_init(dgb_dt, RFNM_LIME0_NUM_LATCHES); 


	


	LMS7002M_t *lms;
    //LMS7_set_log_level(LMS7_DEBUG);
    lms = LMS7002M_create(lms_interface_transact, spi);
    if (lms == NULL) {
		kernel_neon_end();
        return -1;
    }
	dgb_dt->priv_drv = lms;
    LMS7002M_reset(lms);
    LMS7002M_set_spi_mode(lms, 4); //set 4-wire spi before reading back

    //read info register
    LMS7002M_regs_spi_read(lms, 0x002f);
    //printk("rev 0x%x\n", LMS7002M_regs(lms)->reg_0x002f_rev);
    //printk("ver 0x%x\n", LMS7002M_regs(lms)->reg_0x002f_ver);

    if(!LMS7002M_regs(lms)->reg_0x002f_rev) {
        printk("LMS Not Found!");
		kernel_neon_end();
        return -1;
    }

	for(i = 0; i < RFNM_LMS_REGS_CNT_A; i++) {
        uint16_t addr, value;
        addr = rfnm_lms_init_regs_a[i][0];
        value = rfnm_lms_init_regs_a[i][1];

        LMS7002M_set_mac_ch(lms, LMS_CHA);
        LMS7002M_spi_write(lms, addr, value);
        LMS7002M_regs_set(lms->regs, addr, value);
        //printk("Load: 0x%04x=0x%04x\n", addr, value);
    }

    for(i = 0; i < RFNM_LMS_REGS_CNT_B; i++) {
        uint16_t addr, value;
        addr = rfnm_lms_init_regs_b[i][0];
        value = rfnm_lms_init_regs_b[i][1];

        LMS7002M_set_mac_ch(lms, LMS_CHB);
        LMS7002M_spi_write(lms, addr, value);
        LMS7002M_regs_set(lms->regs, addr, value);
        //printk("Load: 0x%04x=0x%04x\n", addr, value);
    }

    //turn the clocks on
    double actualRate = 0.0;
    ret = LMS7002M_set_data_clock(lms, LMS_REF_FREQ, LMS_REF_FREQ, &actualRate);
    if (ret != 0) {
        printk("clock tune failure %d\n", ret);
		kernel_neon_end();
        return -1;
    }

    // rx path is always enabled
    LMS7002M_rbb_enable(lms, LMS_CHA, true);
    LMS7002M_sxx_enable(lms, LMS_RX, true);
    LMS7002M_rfe_enable(lms, LMS_CHA, true);

	
	LMS7002M_sxx_enable(lms, LMS_TX, true);
	LMS7002M_tbb_enable(lms, LMS_CHA, true);
	LMS7002M_trf_enable(lms, LMS_CHA, true);

	// tune tx PLL to a high frequency out of the way to avoid spurs... meh. 
	LMS7002M_set_lo_freq(lms, LMS_TX, LMS_REF_FREQ, MHZ_TO_HZ(3800), &actualRate);
	// then disable some of it
	LMS7002M_sxx_enable(lms, LMS_TX, false);


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


	printk("RFNM: Lime daughterboard initialized\n");

	kernel_neon_end();


	struct rfnm_api_tx_ch *tx_ch, *tx_s;
	struct rfnm_api_rx_ch *rx_ch, *rx_s;

	tx_ch = devm_kzalloc(dev, sizeof(struct rfnm_api_tx_ch), GFP_KERNEL);
	rx_ch = devm_kzalloc(dev, sizeof(struct rfnm_api_rx_ch), GFP_KERNEL);
	tx_s = devm_kzalloc(dev, sizeof(struct rfnm_api_tx_ch), GFP_KERNEL);		
	rx_s = devm_kzalloc(dev, sizeof(struct rfnm_api_rx_ch), GFP_KERNEL);

	if(!tx_ch || !rx_ch || !tx_s || !rx_s) {
		kernel_neon_end();
		return -ENOMEM;
	}

	tx_ch->freq_max = MHZ_TO_HZ(3500);
	tx_ch->freq_min = MHZ_TO_HZ(10);
	tx_ch->path_preferred = RFNM_PATH_SMA_A;
	tx_ch->dac_id = 0;
	rfnm_dgb_reg_tx_ch(dgb_dt, tx_ch, tx_s);

	rx_ch->freq_max = MHZ_TO_HZ(3500);
	rx_ch->freq_min = MHZ_TO_HZ(10);
	rx_ch->path_preferred = RFNM_PATH_SMA_A;
	rx_ch->adc_id = 0;
	rfnm_dgb_reg_rx_ch(dgb_dt, rx_ch, rx_s);

	dgb_dt->dac_ifs = 0x7;
	dgb_dt->dac_iqswap[0] = 0;
	dgb_dt->dac_iqswap[1] = 0;
	//dgb_dt->adc_iqswap[0] = 0;
	dgb_dt->adc_iqswap[0] = 1;
	dgb_dt->adc_iqswap[1] = 0;
	rfnm_dgb_reg(dgb_dt);

	
	return 0;
}

static int rfnm_lime_remove(struct spi_device *spi)
{

	struct rfnm_dgb *dgb_dt;
	dgb_dt = spi_get_drvdata(spi);

	//struct spi_controller *ctlr;

	//ctlr = dev_get_drvdata(&pdev->dev);
	//spi_unregister_controller(ctlr);
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
