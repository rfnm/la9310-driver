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
#include "SiIceWing_parser.h"
#include "SiIceWing_Core.h"
#include "SiDrv.h"

#include <linux/i2c.h>

//#include "fe_generic.h"
//#include "fe_granita.h"

#include "rfnm_granita.h"

char SiCoreChar[3] = {'a', 'b', 0};

struct i2c_client *si5510_i2c_client;
struct device *si5510_i2c_dev;

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



void rfnm_granita_set_bias_t(int dgb_id, int ch, enum rfnm_bias_tee bias_tee) {
	if(ch == 0) {
		if(bias_tee == RFNM_BIAS_TEE_ON) {
			rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO4_3); // FF_ANT_BIAS_A
		} else {
			rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO4_3); // FF_ANT_BIAS_A
		}
	} else {
		if(bias_tee == RFNM_BIAS_TEE_ON) {
			rfnm_gpio_clear(dgb_id, RFNM_DGB_GPIO5_16); // FF_ANT_BIAS_B
		} else {
			rfnm_gpio_set(dgb_id, RFNM_DGB_GPIO5_16); // FF_ANT_BIAS_B
		}
	}
}

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
		if(mhz >= 1) {
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

#define SIAPI_PATH_B (1 << 1)
#define SIAPI_PATH_A (1 << 3)
#define SIAPI_PATH_ALL (SIAPI_PATH_B | SIAPI_PATH_A)


int rfnm_granita_tdd(struct rfnm_dgb *dgb_dt, struct rfnm_api_tx_ch * tx_ch, struct rfnm_api_rx_ch * rx_ch) {
	int ret;
	rfnm_api_failcode ecode = RFNM_API_OK;
	printk("RFNM: TDD rx %d tx %d\n", HZ_TO_KHZ(tx_ch->freq), HZ_TO_KHZ(rx_ch->freq));
	ret = SiAPILoopback(SiCoreChar[dgb_dt->dgb_id], SIAPI_PATH_ALL, HZ_TO_KHZ(tx_ch->freq), HZ_TO_KHZ(rx_ch->freq));
	if(ret) {
		printk("SiAPILoopback wouldn't return nice things\n");
		ecode = RFNM_API_TUNE_FAIL;
		goto fail;
	}

	ret = SiAPIRXGAIN(SiCoreChar[dgb_dt->dgb_id], SIAPI_PATH_ALL, rx_ch->gain);
	if(ret) {
		printk("SiAPIRXGAIN failed\n");
		ecode = RFNM_API_GAIN_FAIL;
		goto fail;
	}

	ret = SiAPITXGAIN(SiCoreChar[dgb_dt->dgb_id], SIAPI_PATH_B, /*tx_ch->power*/ 48);
	if(ret) {
		printk("SiAPITXGAIN wouldn't return nice things\n");
		ecode = RFNM_API_GAIN_FAIL;
		goto fail;
	}

	return 0;
fail:
	return -ecode;	
}



int rfnm_tx_ch_set(struct rfnm_dgb *dgb_dt, struct rfnm_api_tx_ch * tx_ch) {

	int ret;
	rfnm_api_failcode ecode = RFNM_API_OK;

	granita0_tx_freqsel(dgb_dt, HZ_TO_MHZ(tx_ch->freq));
	
	if(tx_ch->path == RFNM_PATH_SMA_A) {
		granita0_ant_a_tx(dgb_dt);
	} else if(tx_ch->path == RFNM_PATH_SMA_B) {
		granita0_ant_b_tx(dgb_dt);
	}

	if(tx_ch->path == RFNM_PATH_LOOPBACK) {
		granita0_tx_power(dgb_dt, HZ_TO_MHZ(tx_ch->freq), -24, 1);
	} else {
		granita0_tx_power(dgb_dt, HZ_TO_MHZ(tx_ch->freq), tx_ch->power, 0);
	}


	if(tx_ch->path == RFNM_PATH_LOOPBACK) {
		// if the path is loopback, it is of PARAMOUNT importance to keep tight
		// control over the amplifiers and chip output power. 

		// Two options:
		// If power is > 0, disable both amplifiers and set switching attenuation to zero
		// if power is zero, we are in loopback calibration mode, enable some 
		// switchable attenuation and also enable the second 20 dB amplifier. 

		if(tx_ch->power > 0) {
			granita0_tx_loopback(dgb_dt, 0);
		} else {
			granita0_tx_loopback(dgb_dt, 1);
		}
	}

	rfnm_granita_set_bias_t(dgb_dt->dgb_id, tx_ch->dgb_ch_id, tx_ch->bias_tee);

	rfnm_fe_load_latches(dgb_dt);
	rfnm_fe_trigger_latches(dgb_dt);



	if(tx_ch->path != RFNM_PATH_LOOPBACK && tx_ch->enable != RFNM_CH_ON_TDD) {
		ret = SiAPIPowerUpTX(SiCoreChar[dgb_dt->dgb_id], SIAPI_PATH_B, HZ_TO_KHZ(tx_ch->freq), parse_granita_iq_lpf(tx_ch->rfic_lpf_bw));
		if(ret) {
			printk("SiAPIPowerUpTX wouldn't return nice things\n");
			ecode = RFNM_API_TUNE_FAIL;
			goto fail;
		}

		ret = SiAPITXGAIN(SiCoreChar[dgb_dt->dgb_id], SIAPI_PATH_B, 48);
		if(ret) {
			printk("SiAPITXGAIN wouldn't return nice things\n");
			ecode = RFNM_API_GAIN_FAIL;
			goto fail;
		}
	}
	else if(tx_ch->enable != RFNM_CH_ON_TDD) {
		// loopback rx frequency is the first rx channel with rx loopback mode set
		long rx_freq = 0;
		for(int q = 0; q < 4; q++) {
			if(dgb_dt->rx_ch[q]->path == RFNM_PATH_LOOPBACK) {
				rx_freq = dgb_dt->rx_ch[q]->freq;
				break;
			}
		}
		if(!rx_freq) {
			printk("You need to set the RX frequency on a RX channel using the loopback path before calling for tx loopback\n");
			ecode = RFNM_API_TUNE_FAIL;
			goto fail;
		}
		printk("%d %d\n", HZ_TO_KHZ(tx_ch->freq), HZ_TO_KHZ(rx_freq));
		ret = SiAPILoopback(SiCoreChar[dgb_dt->dgb_id], SIAPI_PATH_ALL, HZ_TO_KHZ(tx_ch->freq), HZ_TO_KHZ(rx_freq));
		if(ret) {
			printk("SiAPILoopback wouldn't return nice things\n");
			ecode = RFNM_API_TUNE_FAIL;
			goto fail;
		}

		ret = SiAPIRXGAIN(SiCoreChar[dgb_dt->dgb_id], SIAPI_PATH_ALL, 0);
		if(ret) {
			printk("SiAPIRXGAIN failed\n");
			ecode = RFNM_API_GAIN_FAIL;
			goto fail;
		}

		ret = SiAPITXGAIN(SiCoreChar[dgb_dt->dgb_id], SIAPI_PATH_B, tx_ch->power);
		if(ret) {
			printk("SiAPITXGAIN wouldn't return nice things\n");
			ecode = RFNM_API_GAIN_FAIL;
			goto fail;
		}
	}


	if(tx_ch->enable == RFNM_CH_ON_TDD) {
		memcpy(&dgb_dt->fe_tdd[RFNM_TX], &dgb_dt->fe, sizeof(struct fe_s));	

		for(int i = 0; i < 2; i++) {
			if(dgb_dt->rx_s[i]->enable == RFNM_CH_ON_TDD) {
				rfnm_dgb_en_tdd(dgb_dt, tx_ch, dgb_dt->rx_s[i]);
				rfnm_granita_tdd(dgb_dt, tx_ch, dgb_dt->rx_s[i]);
				break;
			}
		}
	}

	memcpy(dgb_dt->tx_s[0], dgb_dt->tx_ch[0], sizeof(struct rfnm_api_tx_ch));

	return 0;
fail: 
	return -ecode;
}

/*[ 2157.510915] Unable to handle kernel paging request at virtual address 0000000200000000
[ 2157.510934] Mem abort info:
[ 2157.510935]   ESR = 0x96000044
[ 2157.510938]   EC = 0x25: DABT (current EL), IL = 32 bits
[ 2157.510943]   SET = 0, FnV = 0
[ 2157.510945]   EA = 0, S1PTW = 0
[ 2157.510947]   FSC = 0x04: level 0 translation fault
[ 2157.510950] Data abort info:
[ 2157.510952]   ISV = 0, ISS = 0x00000044
[ 2157.510954]   CM = 0, WnR = 1
[ 2157.510957] user pgtable: 4k pages, 48-bit VAs, pgdp=0000000105861000
[ 2157.510961] [0000000200000000] pgd=0000000000000000, p4d=0000000000000000
[ 2157.510969] Internal error: Oops: 96000044 [#1] PREEMPT_RT SMP
[ 2157.510974] Modules linked in: rfnm_granita(O) rfnm_breakout(O) rfnm_lime(O) rfnm_usb_boost(O) rfnm_usb(O) rfnm_usb_function(O) rfnm_daughterboard(O) rfnm_lalib(O) la9310rfnm(O) rfnm_gpio(O) kpage_ncache(O) la9310shiva(O) overlay fsl_jr_uio caam_jr caamkeyblob_desc caamhash_desc caamalg_desc crypto_engine authenc libdes cdc_acm crct10dif_ce dw_hdmi_cec snd_soc_imx_hdmi snd_soc_fsl_xcvr caam secvio error fuse [last unloaded: rfnm_gpio]
[ 2157.511031] CPU: 0 PID: 1260 Comm: kworker/0:0 Tainted: G        W  O      5.15.71-rt51 #13
[ 2157.511037] Hardware name: RFNM imx8mp (DT)
[ 2157.511041] Workqueue: events rfnm_apply_dev_rx_chlist_work [rfnm_daughterboard]
[ 2157.511060] pstate: 60000005 (nZCv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[ 2157.511065] pc : ktime_get_real_ts64+0x40/0x100
[ 2157.511077] lr : spi_transfer_one_message+0xe0/0x470
[ 2157.511083] sp : ffff80001bc2b3b0
[ 2157.511085] x29: ffff80001bc2b3b0 x28: ffff80001bc2b5f8 x27: ffff800009843078
[ 2157.511092] x26: 0000000000000000 x25: ffff0000c0fd9390 x24: 0000000000000000
[ 2157.511099] x23: ffff0000c0fecbc8 x22: 0000000200000000 x21: 0000000000106d0e
[ 2157.511106] x20: ffff800009e66f80 x19: 0000000000000000 x18: 00000000000a6c68
[ 2157.511113] x17: 0000000000000000 x16: 0000000000000000 x15: 0000000200000000
[ 2157.511119] x14: 00000000000001ad x13: 0000000000000000 x12: 0000000000000000
[ 2157.511126] x11: 0000000000000000 x10: 0000000000000101 x9 : 0000000000000014
[ 2157.511132] x8 : ffff0000c0fec800 x7 : ffff80001bc2b560 x6 : 00000000000f4240
[ 2157.511139] x5 : ffff0000c0fd9700 x4 : ffff0000c0fecbe0 x3 : 0000000000000000
[ 2157.511145] x2 : 0000000000000000 x1 : ffff800009ddb000 x0 : 00000000663fcdc2
[ 2157.511152] Call trace:
[ 2157.511155]  ktime_get_real_ts64+0x40/0x100
[ 2157.511161]  spi_transfer_one_message+0xe0/0x470
[ 2157.511166]  __spi_pump_messages+0x318/0x59c
[ 2157.511171]  __spi_sync+0x210/0x240
[ 2157.511175]  spi_sync+0x30/0x5c
[ 2157.511179]  spi_sync_transfer.constprop.0+0x68/0x90 [rfnm_granita]
[ 2157.511195]  com_receive+0x8c/0xf0 [rfnm_granita]
[ 2157.511206]  Si_Read_FullPath+0x44/0x64 [rfnm_granita]
[ 2157.511218]  Si_Read_FullAddr+0x68/0x8c [rfnm_granita]
[ 2157.511230]  SiMid_BitWrite+0x6c/0x15c [rfnm_granita]
[ 2157.511242]  SiAPISetSynth+0x6c4/0x900 [rfnm_granita]
[ 2157.511254]  SiAPIPowerUpRX+0x28c/0x6f0 [rfnm_granita]
[ 2157.511266]  rfnm_rx_ch_set+0x21c/0x3c0 [rfnm_granita]
[ 2157.511278]  rfnm_dgb_rx_set+0x1c/0xc4 [rfnm_daughterboard]
[ 2157.511288]  rfnm_apply_dev_rx_chlist_work+0x100/0x12c [rfnm_daughterboard]
[ 2157.511297]  process_one_work+0x1d0/0x354
[ 2157.511302]  worker_thread+0x130/0x460
[ 2157.511306]  kthread+0x188/0x1a0
[ 2157.511312]  ret_from_fork+0x10/0x20
[ 2157.511321] Code: 120002b3 370004b5 d50339bf f9404280 (f90002c0)
[ 2157.511328] ---[ end trace 0000000000000003 ]---


http://mail.spinics.net/lists/linux-spi/msg34523.html

*/
void rfnm_si5510_set_output_status(struct i2c_client *client, int output_id, int enable_disable);

int rfnm_rx_ch_set(struct rfnm_dgb *dgb_dt, struct rfnm_api_rx_ch * rx_ch) {

	int dbm = rx_ch->gain;
	int ret;
	int gr_api_id;
	rfnm_api_failcode ecode = RFNM_API_OK;

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
			granita0_tx_power(dgb_dt, -100, 0, 0);
			granita0_ant_b_crossover(dgb_dt);
		}

		if(rx_ch->path == RFNM_PATH_EMBED_ANT) {
			granita0_tx_power(dgb_dt, HZ_TO_MHZ(rx_ch->freq), -100, 0);
			if(HZ_TO_MHZ(rx_ch->freq) < 3000) {
				granita0_ant_a_embeded_lf(dgb_dt);
			} else {
				granita0_ant_a_embeded_hf(dgb_dt);
			}
		}

		if(rx_ch->path == RFNM_PATH_LOOPBACK) {
			granita0_ant_a_loopback(dgb_dt);
		}

		granita0_fa(dgb_dt, HZ_TO_MHZ(rx_ch->freq), rx_ch->fm_notch);
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
				//printk("granita0_ant_b_through\n");
			}
		}

		if(rx_ch->path == RFNM_PATH_SMA_A) {
			granita0_tx_power(dgb_dt, HZ_TO_MHZ(rx_ch->freq), -100, 0);
			granita0_ant_a_crossover(dgb_dt);
		}


		if(rx_ch->path == RFNM_PATH_EMBED_ANT) {
			granita0_tx_power(dgb_dt, HZ_TO_MHZ(rx_ch->freq), -100, 0);
			if(HZ_TO_MHZ(rx_ch->freq) < 3000) {
				granita0_ant_b_embeded_lf(dgb_dt);
			} else {
				granita0_ant_b_embeded_hf(dgb_dt);
			}
		}

		if(rx_ch->path == RFNM_PATH_LOOPBACK) {
			granita0_ant_b_loopback(dgb_dt);
		}
		
		granita0_fb(dgb_dt, HZ_TO_MHZ(rx_ch->freq), rx_ch->fm_notch);
	}

	rfnm_granita_set_bias_t(dgb_dt->dgb_id, rx_ch->dgb_ch_id, rx_ch->bias_tee);

	if(rx_ch->dgb_ch_id == 0) {
		gr_api_id = SIAPI_PATH_A;
	} else {
		gr_api_id = SIAPI_PATH_B;
	}

	uint64_t freq = rx_ch->freq;

	if(rx_ch->freq < MHZ_TO_HZ(600)) {

		if(dgb_dt->dgb_id == 0) {
		//	rfnm_si5510_set_output_status(si5510_i2c_client, 15, 1);
		} else {
		//	rfnm_si5510_set_output_status(si5510_i2c_client, 2, 1);			
		}

		rfnm_rffc_init(dgb_dt);

		int tsf;

		if(rx_ch->dgb_ch_id == 0) {
			granita0_rffc_rx_a(dgb_dt);
			if(rx_ch->rfic_lpf_bw && rx_ch->rfic_lpf_bw <= 2) {
				tsf = 1615;
				granita0_rffc_rx_a_1574_1576(dgb_dt);
			} else if(rx_ch->rfic_lpf_bw && rx_ch->rfic_lpf_bw <= 30) {
				tsf = 1615;
				granita0_rffc_rx_a_1574_1605(dgb_dt);
			} else {
				tsf = 1198;
				granita0_rffc_rx_a_1166_1229(dgb_dt);
			}
		} else {
			granita0_rffc_rx_b(dgb_dt);
			if(rx_ch->rfic_lpf_bw && rx_ch->rfic_lpf_bw <= 2) {
				tsf = 1615;
				granita0_rffc_rx_b_1574_1576(dgb_dt);
			} else if(rx_ch->rfic_lpf_bw && rx_ch->rfic_lpf_bw <= 30) {
				tsf = 1615;
				granita0_rffc_rx_b_1574_1605(dgb_dt);
			} else {
				tsf = 1198;
				granita0_rffc_rx_b_1166_1229(dgb_dt);
			}
		}

		rfnm_rffc_set_freq(dgb_dt, MHZ_TO_HZ(tsf) - rx_ch->freq);
		freq = MHZ_TO_HZ(tsf);

	} else {
		rfnm_rffc_deinit(dgb_dt);

		if(dgb_dt->dgb_id == 0) {
		//	rfnm_si5510_set_output_status(si5510_i2c_client, 15, 0);
		} else {
		//	rfnm_si5510_set_output_status(si5510_i2c_client, 2, 0);			
		}
	}

#if 0
	rfnm_gpio_output(0, RFNM_GPIO4_26); 
	rfnm_gpio_output(0, RFNM_GPIO4_30); 

	if(rx_ch->dgb_ch_id == 1) {
		rfnm_gpio_clear(0, RFNM_GPIO4_26); 
		rfnm_gpio_set(0, RFNM_GPIO4_30); 
	} else {
		rfnm_gpio_set(0, RFNM_GPIO4_26); 
		rfnm_gpio_clear(0, RFNM_GPIO4_30); 
	}
#endif

	rfnm_fe_load_latches(dgb_dt);
	rfnm_fe_trigger_latches(dgb_dt);

	if(rx_ch->path != RFNM_PATH_LOOPBACK && rx_ch->enable != RFNM_CH_ON_TDD) {
		// TX command takes care of RX init when in loopback mode
		ret = SiAPIPowerUpRX(SiCoreChar[dgb_dt->dgb_id], gr_api_id, HZ_TO_KHZ(freq), parse_granita_iq_lpf(rx_ch->rfic_lpf_bw));
		if(ret) {
			ecode = RFNM_API_TUNE_FAIL;
			goto fail;
			//printf("SiAPIPowerUpRX wouldn't return nice things\n");
		}

		//printk("rx dbm is %d\n", dbm);

		if(dbm > 0) {
			if(dbm > 64) {
				dbm = 64;
			}
			ret = SiAPIRXGAIN(SiCoreChar[dgb_dt->dgb_id], gr_api_id, dbm);
			if(ret) {
				printk("SiAPIRXGAIN failed\n");
				ecode = RFNM_API_GAIN_FAIL;
				goto fail;
			}
		}
	}

	if(rx_ch->enable == RFNM_CH_ON_TDD) {
		// go for maximum attenuation on tx path
		granita0_tx_power(dgb_dt, 1000, -100, 0);
		
		memcpy(&dgb_dt->fe_tdd[RFNM_RX], &dgb_dt->fe, sizeof(struct fe_s));	
		if(dgb_dt->tx_s[0]->enable == RFNM_CH_ON_TDD) {
			rfnm_dgb_en_tdd(dgb_dt, dgb_dt->tx_s[0], rx_ch);
			rfnm_granita_tdd(dgb_dt, dgb_dt->tx_s[0], rx_ch);
		}
	}

	memcpy(dgb_dt->rx_s[rx_ch->dgb_ch_id], dgb_dt->rx_ch[rx_ch->dgb_ch_id], sizeof(struct rfnm_api_rx_ch));	

	return 0;

	
fail: 
	//if(ecode)
	//	printk("freq %ld retcode %d\n",freq,  ecode);
	return -ecode;
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

	rfnm_gpio_output(dgb_id, RFNM_DGB_GPIO4_3); // FF_ANT_BIAS_A
	rfnm_gpio_output(dgb_id, RFNM_DGB_GPIO5_16); // FF_ANT_BIAS_B

	rfnm_granita_set_bias_t(dgb_id, 0, RFNM_BIAS_TEE_OFF);
	rfnm_granita_set_bias_t(dgb_id, 1, RFNM_BIAS_TEE_OFF);

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
	spi->bits_per_word = 8;
	spi->mode = SPI_CPHA;

	rfnm_fe_generic_init(dgb_dt, RFNM_GRANITA0_NUM_LATCHES); 


    dgb_dt->priv_drv = kmalloc (100, GFP_KERNEL );

	

	

    ret = SiAPIOpen(SiCoreChar[dgb_dt->dgb_id], dgb_dt->priv_drv, spi);
    if(ret) {
        printk("Si core wouldn't open\n");
        return -1;
    }
	//int z;
	//for(z = 0; z < 100; z++) {
	//	ret = SiAPISPITest(SiCoreChar[dgb_dt->dgb_id]);
    //	printk("SiAPISPITest %d\n", ret);
	//}

	ret = SiAPISPITest(SiCoreChar[dgb_dt->dgb_id]);
	if(ret) {
		printk("Si core wouldn't open\n");
		return -1;
		//printf("SiAPIPowerUpRX wouldn't return nice things\n");
	}
	

	//ret = SiAPIPowerUpRX(SiCoreChar[dgb_dt->dgb_id], SIAPI_PATH_A, 1850 * 1000, 100000);
	//if(ret) {
	//	return -1;
		//printf("SiAPIPowerUpRX wouldn't return nice things\n");
	//}

	printk("RFNM: Granita daughterboard initialized\n");

	//rfnm_daughterboard_register_cb(slot, RFNM_CB_RX_TUNE);


	
	si5510_i2c_dev = bus_find_device_by_name(&i2c_bus_type, NULL, "0-0058");
	if (!si5510_i2c_dev) {
		printk("Couldn't find i2c device\n");
	} else {
		si5510_i2c_client = i2c_verify_client(si5510_i2c_dev);
		if (!si5510_i2c_client) {
			printk("Couldn't find i2c client\n");
		}
	}
	


	
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
	tx_ch->freq_min = MHZ_TO_HZ(600);
	tx_ch->path_preferred = RFNM_PATH_SMA_B;
	tx_ch->path_possible[0] = RFNM_PATH_SMA_B;
	tx_ch->path_possible[1] = RFNM_PATH_SMA_A;
	tx_ch->path_possible[2] = RFNM_PATH_NULL;
	tx_ch->power_range.min = -60;
	tx_ch->power_range.max = 30;

	tx_ch->dac_id = 0;
	rfnm_dgb_reg_tx_ch(dgb_dt, tx_ch, tx_s);

	rx_ch[0]->freq_max = MHZ_TO_HZ(6300);
	rx_ch[0]->freq_min = MHZ_TO_HZ(600);
	rx_ch[0]->path_preferred = RFNM_PATH_SMA_A;
	rx_ch[0]->path_possible[0] = RFNM_PATH_SMA_A;
	rx_ch[0]->path_possible[1] = RFNM_PATH_SMA_B;
	rx_ch[0]->path_possible[2] = RFNM_PATH_EMBED_ANT;
	rx_ch[0]->path_possible[3] = RFNM_PATH_NULL;
	rx_ch[0]->gain_range.min = -40;
	rx_ch[0]->gain_range.max = 60;

	rx_ch[0]->adc_id = 1;
	rfnm_dgb_reg_rx_ch(dgb_dt, rx_ch[0], rx_s[0]);
	
	rx_ch[1]->freq_max = MHZ_TO_HZ(6300);
	rx_ch[1]->freq_min = MHZ_TO_HZ(600);
	rx_ch[1]->path_preferred = RFNM_PATH_SMA_B;
	rx_ch[1]->path_possible[0] = RFNM_PATH_SMA_B;
	rx_ch[1]->path_possible[1] = RFNM_PATH_SMA_A;
	rx_ch[1]->path_possible[2] = RFNM_PATH_EMBED_ANT;
	rx_ch[1]->path_possible[3] = RFNM_PATH_NULL;
	rx_ch[0]->gain_range.min = -40;
	rx_ch[0]->gain_range.max = 60;

	rx_ch[1]->adc_id = 0;
	rfnm_dgb_reg_rx_ch(dgb_dt, rx_ch[1], rx_s[1]);

/*
		cfg->daughterboard_eeprom[dgb_id].board_id, 
		cfg->daughterboard_eeprom[dgb_id].board_revision_id, 
		cfg->daughterboard_eeprom[dgb_id].serial_number*/

	dgb_dt->dac_ifs = 0xf;
	dgb_dt->dac_iqswap[0] = 1;
	dgb_dt->dac_iqswap[1] = 0;
	dgb_dt->adc_iqswap[0] = 0;
	dgb_dt->adc_iqswap[1] = 0;
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

	put_device(si5510_i2c_dev);

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
