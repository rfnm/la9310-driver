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

struct rfnm_dgb *rfnm_dgb[2];
struct rfnm_bootconfig *bootcfg;
volatile struct rfnm_m7_dgb *m7_dgb;
volatile uint32_t *dcs_vmem;
volatile uint32_t *gpout_vmem;

uint8_t rfnm_rx_adc_s[4];
uint8_t rfnm_tx_dac_s;

int abs_ch_cnt = 0;


void rfnm_dgb_reg_rx_ch(struct rfnm_dgb *dgb_dt, struct rfnm_api_rx_ch * rx_ch, struct rfnm_api_rx_ch * rx_s) {
	int dgb_slot = dgb_dt->dgb_id;
	rfnm_dgb[dgb_slot] = dgb_dt;
	rx_ch->dgb_ch_id = rfnm_dgb[dgb_slot]->rx_ch_cnt;
	rx_ch->abs_id = abs_ch_cnt++;
	rfnm_dgb[dgb_slot]->rx_ch[rx_ch->dgb_ch_id] = rx_ch;
	rfnm_dgb[dgb_slot]->rx_s[rx_ch->dgb_ch_id] = rx_s;
	rfnm_dgb[dgb_slot]->rx_ch_cnt++;
}
EXPORT_SYMBOL(rfnm_dgb_reg_rx_ch);

void rfnm_dgb_reg_tx_ch(struct rfnm_dgb *dgb_dt, struct rfnm_api_tx_ch * tx_ch, struct rfnm_api_tx_ch * tx_s) { 
	int dgb_slot = dgb_dt->dgb_id;
	rfnm_dgb[dgb_slot] = dgb_dt;
	tx_ch->dgb_ch_id = rfnm_dgb[dgb_slot]->tx_ch_cnt;
	tx_ch->abs_id = abs_ch_cnt++;
	rfnm_dgb[dgb_slot]->tx_ch[tx_ch->dgb_ch_id] = tx_ch;
	rfnm_dgb[dgb_slot]->tx_s[tx_ch->dgb_ch_id] = tx_s;
	rfnm_dgb[dgb_slot]->tx_ch_cnt++;
}
EXPORT_SYMBOL(rfnm_dgb_reg_tx_ch);


void rfnm_populate_dev_hwinfo(struct rfnm_dev_hwinfo * r_hwinfo) {
	int i;

	memset(r_hwinfo, 0, sizeof(struct rfnm_dev_hwinfo));

	r_hwinfo->protocol_version = 1;
	r_hwinfo->clock.dcs_clk = MHZ_TO_HZ(122.88);

	r_hwinfo->motherboard.board_id = bootcfg->motherboard_eeprom.board_id;
	r_hwinfo->motherboard.board_revision_id = bootcfg->motherboard_eeprom.board_revision_id;
	
	memcpy(&r_hwinfo->motherboard.serial_number[0], &bootcfg->motherboard_eeprom.serial_number[0], 9);
	memcpy(&r_hwinfo->motherboard.mac_addr[0], &bootcfg->motherboard_eeprom.mac_addr[0], 6);
	memcpy(&r_hwinfo->motherboard.user_readable_name[0], RFNM_BOARD_ID_TO_USER_READABLE_NAME[bootcfg->motherboard_eeprom.board_id], 30);
	
	for(i = 0; i < 2; i++) {
		if(!rfnm_dgb[i]) {
			continue;
		}

		r_hwinfo->motherboard.tx_ch_cnt += rfnm_dgb[i]->tx_ch_cnt;
		r_hwinfo->motherboard.rx_ch_cnt += rfnm_dgb[i]->rx_ch_cnt;

		r_hwinfo->daughterboard[i].tx_ch_cnt = rfnm_dgb[i]->tx_ch_cnt;
		r_hwinfo->daughterboard[i].rx_ch_cnt = rfnm_dgb[i]->rx_ch_cnt;

		r_hwinfo->daughterboard[i].board_id = bootcfg->daughterboard_eeprom[i].board_id;
		r_hwinfo->daughterboard[i].board_revision_id = bootcfg->daughterboard_eeprom[i].board_revision_id;
		memcpy(&r_hwinfo->daughterboard[i].serial_number[0], &bootcfg->daughterboard_eeprom[i].serial_number[0], 9);
		memcpy(&r_hwinfo->daughterboard[i].user_readable_name[0], RFNM_BOARD_ID_TO_USER_READABLE_NAME[bootcfg->daughterboard_eeprom[i].board_id], 30);
	}


	
	// int16_t r_hwinfo->temperature;;
	

	
}
EXPORT_SYMBOL(rfnm_populate_dev_hwinfo);


void rfnm_populate_dev_tx_chlist(struct rfnm_dev_tx_ch_list * r_chlist) {
	int i, q, d = 0;

	for(i = 0; i < 2; i++) {
		if(!rfnm_dgb[i]) {
			continue;
		}
		for(q = 0; q < rfnm_dgb[i]->tx_ch_cnt; q++) {
			memcpy(&r_chlist->ch[d], rfnm_dgb[i]->tx_ch[q], sizeof(struct rfnm_api_tx_ch));
			d++;
		}
	}

	for(i = d; i < 8; i++) {
		memset(&r_chlist->ch[i], 0, sizeof(struct rfnm_api_tx_ch));
	}
}
EXPORT_SYMBOL(rfnm_populate_dev_tx_chlist);

void rfnm_populate_dev_rx_chlist(struct rfnm_dev_rx_ch_list * r_chlist) {
	int i, q, d = 0;

	for(i = 0; i < 2; i++) {
		if(!rfnm_dgb[i]) {
			continue;
		}
		for(q = 0; q < rfnm_dgb[i]->rx_ch_cnt; q++) {
			memcpy(&r_chlist->ch[d], rfnm_dgb[i]->rx_ch[q], sizeof(struct rfnm_api_rx_ch));
			d++;
		}
	}

	for(i = d; i < 8; i++) {
		memset(&r_chlist->ch[i], 0, sizeof(struct rfnm_api_rx_ch));
	}
}
EXPORT_SYMBOL(rfnm_populate_dev_rx_chlist);

struct rfnm_dev_get_set_result rfnm_dev_work_res;

void rfnm_populate_dev_set_res(struct rfnm_dev_get_set_result * r_res) {
	memcpy(r_res, &rfnm_dev_work_res, sizeof(struct rfnm_dev_get_set_result));
}
EXPORT_SYMBOL(rfnm_populate_dev_set_res);



int rfnm_dgb_tx_set(struct rfnm_dgb *rfnm_dgb_dt, struct rfnm_api_tx_ch * tx_ch) {
	int (*ch_fun)(struct rfnm_dgb *, struct rfnm_api_tx_ch *);
	ch_fun = rfnm_dgb_dt->tx_ch_set;
	int r = ch_fun(rfnm_dgb_dt, tx_ch);

	if(!r) {
		int stream_rate = 1;
		if(tx_ch->samp_freq_div_n == 2) {
			stream_rate = 2;
		}
		if(tx_ch->stream == RFNM_CH_STREAM_AUTO) {
			if(tx_ch->enable != RFNM_CH_OFF) {
				rfnm_tx_dac_s = stream_rate;
			} else {
				rfnm_tx_dac_s = 0;
			}
		} else {
			if(tx_ch->stream == RFNM_CH_STREAM_ON) {
				rfnm_tx_dac_s = stream_rate;
			} else if(tx_ch->stream == RFNM_CH_STREAM_OFF) {
				rfnm_tx_dac_s = 0;
			}
		}
	}

	return r;
}

int rfnm_dgb_rx_set(struct rfnm_dgb *rfnm_dgb_dt, struct rfnm_api_rx_ch * rx_ch) {
	int (*ch_fun)(struct rfnm_dgb *, struct rfnm_api_rx_ch *);
	ch_fun = rfnm_dgb_dt->rx_ch_set;
	int r = ch_fun(rfnm_dgb_dt, rx_ch);

	if(!r) {
		int stream_rate = 1;
		if(rx_ch->samp_freq_div_n == 2) {
			stream_rate = 2;
		}
		if(rx_ch->stream == RFNM_CH_STREAM_AUTO) {
			if(rx_ch->enable != RFNM_CH_OFF) {
				rfnm_rx_adc_s[rx_ch->adc_id] = stream_rate;
			} else {
				rfnm_rx_adc_s[rx_ch->adc_id] = 0;
			}
		} else {
			if(rx_ch->stream == RFNM_CH_STREAM_ON) {
				rfnm_rx_adc_s[rx_ch->adc_id] = stream_rate;
			} else if(rx_ch->stream == RFNM_CH_STREAM_OFF) {
				rfnm_rx_adc_s[rx_ch->adc_id] = 0;
			}
		}
	}

	return r;
}



struct rfnm_dev_tx_ch_list r_tx_chlist_work;
struct rfnm_dev_rx_ch_list r_rx_chlist_work;

#include <linux/workqueue.h>

void rfnm_apply_dev_tx_chlist_work(struct work_struct * tasklet_data) {
	int i, q, d = 0;

	for(i = 0; i < 2; i++) {
		if(!rfnm_dgb[i]) {
			continue;
		}
		for(q = 0; q < rfnm_dgb[i]->tx_ch_cnt; q++) {
			memcpy(rfnm_dgb[i]->tx_ch[q], &r_tx_chlist_work.ch[d], sizeof(struct rfnm_api_tx_ch));
#if 1
			if (((1 << q) & r_tx_chlist_work.apply)) {
				int ecode = rfnm_dgb_tx_set(rfnm_dgb[i], rfnm_dgb[i]->tx_ch[q]);
				printk("tx ch %d code %d\n", q, ecode);	
				rfnm_dev_work_res.tx_ecodes[q] = -ecode;
			}			
#endif
			d++;
		}
	}
	rfnm_la9310_stream(rfnm_tx_dac_s, rfnm_rx_adc_s);
	rfnm_dev_work_res.cc_rx = r_rx_chlist_work.cc;
}
DECLARE_WORK(rfnm_tx_chlist_work, &rfnm_apply_dev_tx_chlist_work);





void rfnm_apply_dev_rx_chlist_work(struct work_struct * tasklet_data) {
	int i, q, d = 0;

	for(i = 0; i < 2; i++) {
		if(!rfnm_dgb[i]) {
			continue;
		}
		for(q = 0; q < rfnm_dgb[i]->rx_ch_cnt; q++) {
			memcpy(rfnm_dgb[i]->rx_ch[q], &r_rx_chlist_work.ch[d], sizeof(struct rfnm_api_rx_ch));
#if 1
			if (((1 << q) & r_rx_chlist_work.apply)) {
				int ecode = rfnm_dgb_rx_set(rfnm_dgb[i], rfnm_dgb[i]->rx_ch[q]);				
				printk("rx ch %d code %d\n", q, ecode);
				rfnm_dev_work_res.rx_ecodes[q] = -ecode;
			}			
#endif
			d++;
		}
	}

	rfnm_la9310_stream(rfnm_tx_dac_s, rfnm_rx_adc_s);
	rfnm_dev_work_res.cc_rx = r_rx_chlist_work.cc;
}

DECLARE_WORK(rfnm_rx_chlist_work , &rfnm_apply_dev_rx_chlist_work);

void rfnm_apply_dev_tx_chlist(struct rfnm_dev_tx_ch_list * r_chlist) {
	memcpy(&r_tx_chlist_work, r_chlist, sizeof(struct rfnm_dev_tx_ch_list));
	schedule_work(&rfnm_tx_chlist_work);
}
EXPORT_SYMBOL(rfnm_apply_dev_tx_chlist);

void rfnm_apply_dev_rx_chlist(struct rfnm_dev_rx_ch_list * r_chlist) {

	if(work_pending(&rfnm_rx_chlist_work)) {
		printk("Not scheduling work! bug... \n");
	} else {
		memcpy(&r_rx_chlist_work, r_chlist, sizeof(struct rfnm_dev_rx_ch_list));
		schedule_work(&rfnm_rx_chlist_work);
	}
	
}
EXPORT_SYMBOL(rfnm_apply_dev_rx_chlist);






void rfnm_dgb_en_tdd(struct rfnm_dgb *dgb_dt, struct rfnm_api_tx_ch * tx_ch, struct rfnm_api_rx_ch * rx_ch) {

	printk("RFNM: Detected TDD configuration, writing to M7 core, make sure it's running...\n");

	//printk("m7 %lx", (void*)m7_dgb);
	//printk("tdd_avl %lx\n", (void*)&m7_dgb->tdd_available);

	//printk("fe_tdd[0] %lx\n", (void*)&m7_dgb->fe_tdd[RFNM_TX]);
	//printk("fe_tdd[1] %lx\n", (void*)&m7_dgb->fe_tdd[RFNM_RX]);

	memcpy(&m7_dgb->fe_tdd[RFNM_TX], &dgb_dt->fe_tdd[RFNM_TX], sizeof(struct fe_s));
	memcpy(&m7_dgb->fe_tdd[RFNM_RX], &dgb_dt->fe_tdd[RFNM_RX], sizeof(struct fe_s));

	m7_dgb->dgb_id = dgb_dt->dgb_id;
	m7_dgb->tdd_available = 1;

	
	
}
EXPORT_SYMBOL(rfnm_dgb_en_tdd);







struct rfnm_ch_obj {
	struct kobject kobj;

	int dgb_id; 
	int dgb_ch_id;
	int txrx;
};
#define to_rfnm_ch_obj(x) container_of(x, struct rfnm_ch_obj, kobj)

/* a custom attribute that works just for a struct rfnm_ch_obj. */
struct r_attribute {
	struct attribute attr;
	ssize_t (*show)(struct rfnm_ch_obj *foo, struct r_attribute *attr, char *buf);
	ssize_t (*store)(struct rfnm_ch_obj *foo, struct r_attribute *attr, const char *buf, size_t count);
};
#define to_rfnm_rx_attr(x) container_of(x, struct r_attribute, attr)


static ssize_t rfnm_attr_show(struct kobject *kobj,
			     struct attribute *attr,
			     char *buf)
{
	struct r_attribute *attribute;
	struct rfnm_ch_obj *foo;

	attribute = to_rfnm_rx_attr(attr);
	foo = to_rfnm_ch_obj(kobj);

	if (!attribute->show)
		return -EIO;

	return attribute->show(foo, attribute, buf);
}

static ssize_t rfnm_attr_store(struct kobject *kobj,
			      struct attribute *attr,
			      const char *buf, size_t len)
{
	struct r_attribute *attribute;
	struct rfnm_ch_obj *foo;

	attribute = to_rfnm_rx_attr(attr);
	foo = to_rfnm_ch_obj(kobj);

	if (!attribute->store)
		return -EIO;

	return attribute->store(foo, attribute, buf, len);
}

/* Our custom sysfs_ops that we will associate with our ktype later on */
static const struct sysfs_ops rfnm_txrx_sysfs_ops = {
	.show = rfnm_attr_show,
	.store = rfnm_attr_store,
};

static void foo_release(struct kobject *kobj)
{
	struct rfnm_ch_obj *foo;

	foo = to_rfnm_ch_obj(kobj);
	kfree(foo);
}

static ssize_t b_show(struct rfnm_ch_obj *ch_obj, struct r_attribute *attr, char *buf) {
	int var;

	if(strcmp(attr->attr.name, "freq") == 0) {
		if(ch_obj->txrx == RFNM_RX) {
			return sysfs_emit(buf, "%lld\n", rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->freq);
		} else {
			return sysfs_emit(buf, "%lld\n", rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->freq);
		}
	}

	if(strcmp(attr->attr.name, "freq_min") == 0) {
		if(ch_obj->txrx == RFNM_RX) {
			return sysfs_emit(buf, "%lld\n", rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->freq_min);
		} else {
			return sysfs_emit(buf, "%lld\n", rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->freq_min);
		}
	}

	if(strcmp(attr->attr.name, "freq_max") == 0) {
		if(ch_obj->txrx == RFNM_RX) {
			return sysfs_emit(buf, "%lld\n", rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->freq_max);
		} else {
			return sysfs_emit(buf, "%lld\n", rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->freq_max);
		}
	}

	if(strcmp(attr->attr.name, "iq_lpf_bw") == 0) {
		if(ch_obj->txrx == RFNM_RX) {
			return sysfs_emit(buf, "%d\n", rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->iq_lpf_bw);
		} else {
			return sysfs_emit(buf, "%d\n", rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->iq_lpf_bw);
		}
	}

	if(strcmp(attr->attr.name, "power") == 0) {
		return sysfs_emit(buf, "%d\n", rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->power);
	}

	if(strcmp(attr->attr.name, "dac_id") == 0) {
		return sysfs_emit(buf, "%d\n", rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->dac_id);
	}

	if(strcmp(attr->attr.name, "gain") == 0) {
		return sysfs_emit(buf, "%d\n", rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->gain);
	}

	if(strcmp(attr->attr.name, "rfic_dc_q") == 0) {
		return sysfs_emit(buf, "%d\n", rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->rfic_dc_q);
	}

	if(strcmp(attr->attr.name, "rfic_dc_i") == 0) {
		return sysfs_emit(buf, "%d\n", rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->rfic_dc_i);
	}
	
	if(strcmp(attr->attr.name, "adc_id") == 0) {
		return sysfs_emit(buf, "%d\n", rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->adc_id);
	}



	if(strcmp(attr->attr.name, "enable") == 0) {
		enum rfnm_ch_enable enable;
		if(ch_obj->txrx == RFNM_RX) {
			enable = rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->enable;
		} else {
			enable = rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->enable;
		}

		if(enable == RFNM_CH_OFF) {
			return sysfs_emit(buf, "off\n");
		} else if(enable == RFNM_CH_ON) {
			return sysfs_emit(buf, "on\n");
		} else if(enable == RFNM_CH_ON_TDD) {
			return sysfs_emit(buf, "tdd\n");
		}
	}

	if(strcmp(attr->attr.name, "stream") == 0) {
		enum rfnm_ch_stream stream;
		if(ch_obj->txrx == RFNM_RX) {
			stream = rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->stream;
		} else {
			stream = rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->stream;
		}

		if(stream == RFNM_CH_STREAM_AUTO) {
			return sysfs_emit(buf, "auto\n");
		} else if(stream == RFNM_CH_STREAM_OFF) {
			return sysfs_emit(buf, "off\n");
		} else if(stream == RFNM_CH_STREAM_ON) {
			return sysfs_emit(buf, "on\n");
		}
	}
	
	if(strcmp(attr->attr.name, "bias_tee") == 0) {
		enum rfnm_bias_tee bias_tee;
		if(ch_obj->txrx == RFNM_RX) {
			bias_tee = rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->bias_tee;
		} else {
			bias_tee = rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->bias_tee;
		}

		if(bias_tee == RFNM_BIAS_TEE_OFF) {
			return sysfs_emit(buf, "off\n");
		} else if(bias_tee == RFNM_BIAS_TEE_ON) {
			return sysfs_emit(buf, "on\n");
		}
	}

	if(strcmp(attr->attr.name, "fm_notch") == 0) {
		enum rfnm_fm_notch fm_notch;
		fm_notch = rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->fm_notch;
		
		if(fm_notch == RFNM_FM_NOTCH_AUTO) {
			return sysfs_emit(buf, "auto\n");
		} else if(fm_notch == RFNM_FM_NOTCH_ON) {
			return sysfs_emit(buf, "on\n");
		} else if(fm_notch == RFNM_FM_NOTCH_OFF) {
			return sysfs_emit(buf, "off\n");
		}
	}

	if(strcmp(attr->attr.name, "path") == 0) {
		enum rfnm_rf_path path;
		if(ch_obj->txrx == RFNM_RX) {
			path = rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->path;
		} else {
			path = rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->path;
		}

		if(path == RFNM_PATH_SMA_A) {
			return sysfs_emit(buf, "sma_a\n");
		} else if(path == RFNM_PATH_SMA_B) {
			return sysfs_emit(buf, "sma_b\n");
		} else if(path == RFNM_PATH_SMA_C) {
			return sysfs_emit(buf, "sma_c\n");
		} else if(path == RFNM_PATH_SMA_D) {
			return sysfs_emit(buf, "sma_d\n");
		} else if(path == RFNM_PATH_SMA_E) {
			return sysfs_emit(buf, "sma_e\n");
		} else if(path == RFNM_PATH_SMA_F) {
			return sysfs_emit(buf, "sma_f\n");
		} else if(path == RFNM_PATH_SMA_G) {
			return sysfs_emit(buf, "sma_g\n");
		} else if(path == RFNM_PATH_SMA_H) {
			return sysfs_emit(buf, "sma_h\n");
		} else if(path == RFNM_PATH_EMBED_ANT) {
			return sysfs_emit(buf, "embed_ant\n");
		} else if(path == RFNM_PATH_LOOPBACK) {
			return sysfs_emit(buf, "loopback\n");
		}
	}

	if(strcmp(attr->attr.name, "agc") == 0) {
		enum rfnm_agc_type agc_type;
		agc_type = rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->agc;

		if(agc_type == RFNM_AGC_OFF) {
			return sysfs_emit(buf, "off\n");
		} else if(agc_type == RFNM_AGC_DEFAULT) {
			return sysfs_emit(buf, "default\n");
		}
	}

		




	return -EINVAL;
}

static ssize_t b_store(struct rfnm_ch_obj *ch_obj, struct r_attribute *attr, const char *buf, size_t count) {
	long long var; 
	int intconv;
	char buf_red[100];
	if(strlen(buf) > 90) {
		return -EINVAL;
	}
	strcpy(buf_red, buf);
	if(strlen(buf_red) && (buf_red[strlen(buf_red) - 1] == 0xa || buf_red[strlen(buf_red) - 1] == "\n")) {
		buf_red[strlen(buf_red) - 1] = 0;
	}

	intconv = kstrtoll(buf, 10, &var);

	if(strcmp(attr->attr.name, "apply") == 0) {
		if(intconv < 0 || var < 1) {
			return -EINVAL;
		}
		
		if(ch_obj->txrx == RFNM_RX) {
			if(rfnm_dgb_rx_set(rfnm_dgb[ch_obj->dgb_id], rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id])) {
				return -EAGAIN;
			}
		} else {
			if(rfnm_dgb_tx_set(rfnm_dgb[ch_obj->dgb_id], rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id])) {
				return -EAGAIN;
			}
		}
	}

	if(strcmp(attr->attr.name, "freq") == 0) {
		if(intconv < 0) {
			return -EINVAL;
		}
		
		if(ch_obj->txrx == RFNM_RX) {
			rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->freq = var;
		} else {
			rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->freq = var;
		}
	}

	if(strcmp(attr->attr.name, "freq_min") == 0) {
		if(intconv < 0) {
			return -EINVAL;
		}
		
		if(ch_obj->txrx == RFNM_RX) {
			rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->freq_min = var;
		} else {
			rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->freq_min = var;
		}
	}

	if(strcmp(attr->attr.name, "freq_max") == 0) {
		if(intconv < 0) {
			return -EINVAL;
		}
		
		if(ch_obj->txrx == RFNM_RX) {
			rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->freq_max = var;
		} else {
			rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->freq_max = var;
		}
	}

	if(strcmp(attr->attr.name, "iq_lpf_bw") == 0) {
		if(intconv < 0) {
			return -EINVAL;
		}
		
		if(ch_obj->txrx == RFNM_RX) {
			rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->iq_lpf_bw = var;
		} else {
			rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->iq_lpf_bw = var;
		}
	}

	if(strcmp(attr->attr.name, "power") == 0) {
		if(intconv < 0) {
			return -EINVAL;
		}
		rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->power = var;
	}

	if(strcmp(attr->attr.name, "gain") == 0) {
		if(intconv < 0) {
			return -EINVAL;
		}
		rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->gain = var;
	}

	if(strcmp(attr->attr.name, "rfic_dc_q") == 0) {
		if(intconv < 0) {
			return -EINVAL;
		}
		rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->rfic_dc_q = var;
	}

	if(strcmp(attr->attr.name, "rfic_dc_i") == 0) {
		if(intconv < 0) {
			return -EINVAL;
		}
		rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->rfic_dc_i = var;
	}

	if(strcmp(attr->attr.name, "enable") == 0) {
		enum rfnm_ch_enable enable;

		if(strcmp(buf_red, "off") == 0) {
			enable = RFNM_CH_OFF;
		} else if(strcmp(buf_red, "on") == 0) {
			enable = RFNM_CH_ON;
		} else if(strcmp(buf_red, "tdd") == 0) {
			enable = RFNM_CH_ON_TDD;
		} else {
			printk("%d enable, %s\n", enable, buf);
			return -EINVAL;
		}		

		if(ch_obj->txrx == RFNM_RX) {
			rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->enable = enable;
		} else {
			rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->enable = enable;
		}
	}

	if(strcmp(attr->attr.name, "stream") == 0) {
		enum rfnm_ch_stream stream;

		if(strcmp(buf_red, "auto") == 0) {
			stream = RFNM_CH_STREAM_AUTO;
		} else if(strcmp(buf_red, "off") == 0) {
			stream = RFNM_CH_STREAM_OFF;
		} else if(strcmp(buf_red, "on") == 0) {
			stream = RFNM_CH_STREAM_ON;
		} else {
			printk("%d stream, %s\n", stream, buf);
			return -EINVAL;
		}		

		if(ch_obj->txrx == RFNM_RX) {
			rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->stream = stream;
		} else {
			rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->stream = stream;
		}
	}

	if(strcmp(attr->attr.name, "bias_tee") == 0) {
		enum rfnm_bias_tee bias_tee;

		if(strcmp(buf_red, "off") == 0) {
			bias_tee = RFNM_BIAS_TEE_OFF;
		} else if(strcmp(buf_red, "on") == 0) {
			bias_tee = RFNM_BIAS_TEE_ON;
		} else {
			return -EINVAL;
		}		

		if(ch_obj->txrx == RFNM_RX) {
			rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->bias_tee = bias_tee;
		} else {
			rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->bias_tee = bias_tee;
		}
	}

	if(strcmp(attr->attr.name, "fm_notch") == 0) {
		enum rfnm_fm_notch fm_notch;

		if(strcmp(buf_red, "auto") == 0) {
			fm_notch = RFNM_FM_NOTCH_AUTO;
		} else if(strcmp(buf_red, "off") == 0) {
			fm_notch = RFNM_FM_NOTCH_OFF;
		} else if(strcmp(buf_red, "on") == 0) {
			fm_notch = RFNM_FM_NOTCH_ON;
		} else {
			return -EINVAL;
		}		
		
		rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->fm_notch = fm_notch;
	}

	if(strcmp(attr->attr.name, "path") == 0) {
		enum rfnm_rf_path path;

		if(strcmp(buf_red, "sma_a") == 0) {
			path = RFNM_PATH_SMA_A;
		} else if(strcmp(buf_red, "sma_b") == 0) {
			path = RFNM_PATH_SMA_B;
		} else if(strcmp(buf_red, "sma_c") == 0) {
			path = RFNM_PATH_SMA_C;
		} else if(strcmp(buf_red, "sma_d") == 0) {
			path = RFNM_PATH_SMA_D;
		} else if(strcmp(buf_red, "sma_e") == 0) {
			path = RFNM_PATH_SMA_E;
		} else if(strcmp(buf_red, "sma_f") == 0) {
			path = RFNM_PATH_SMA_F;
		} else if(strcmp(buf_red, "sma_g") == 0) {
			path = RFNM_PATH_SMA_G;
		} else if(strcmp(buf_red, "sma_h") == 0) {
			path = RFNM_PATH_SMA_H;
		} else if(strcmp(buf_red, "embed_ant") == 0) {
			path = RFNM_PATH_EMBED_ANT;
		} else if(strcmp(buf_red, "loopback") == 0) {
			path = RFNM_PATH_LOOPBACK;
		} else {
			return -EINVAL;
		}

		if(ch_obj->txrx == RFNM_RX) {
			rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->path = path;
			if(path == RFNM_PATH_LOOPBACK) {
				// disable loopback for every other receive channel 
				// because dgb driver reads the first loopback channel 
				for(int q = 0; q < rfnm_dgb[ch_obj->dgb_id]->rx_ch_cnt; q++) {
					if(q != ch_obj->dgb_ch_id && rfnm_dgb[ch_obj->dgb_id]->rx_ch[q]->path == RFNM_PATH_LOOPBACK) {
						rfnm_dgb[ch_obj->dgb_id]->rx_ch[q]->path = RFNM_PATH_SMA_A;
					}
				}
			}
		} else {
			rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->path = path;
		}
	}




	if(strcmp(attr->attr.name, "agc") == 0) {
		enum rfnm_agc_type agc_type;

		if(strcmp(buf_red, "off") == 0) {
			agc_type = RFNM_AGC_OFF;
		} else if(strcmp(buf_red, "default") == 0) {
			agc_type = RFNM_AGC_DEFAULT;
		} else {
			return -EINVAL;
		}
		
		rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->agc = agc_type;
	}


	



	return count;
}



static struct r_attribute freq_attribute = __ATTR(freq, 0664, b_show, b_store);
static struct r_attribute enable_attribute = __ATTR(enable, 0664, b_show, b_store);
static struct r_attribute gain_attribute = __ATTR(gain, 0664, b_show, b_store);
static struct r_attribute power_attribute = __ATTR(power, 0664, b_show, b_store);
static struct r_attribute apply_attribute = __ATTR(apply, 0664, b_show, b_store);
static struct r_attribute adc_id_attribute = __ATTR(adc_id, 0664, b_show, b_store);
static struct r_attribute dac_id_attribute = __ATTR(dac_id, 0664, b_show, b_store);
static struct r_attribute freq_min_attribute = __ATTR(freq_min, 0664, b_show, b_store);
static struct r_attribute freq_max_attribute = __ATTR(freq_max, 0664, b_show, b_store);
static struct r_attribute iq_lpf_bw_attribute = __ATTR(iq_lpf_bw, 0664, b_show, b_store);
static struct r_attribute agc_attribute = __ATTR(agc, 0664, b_show, b_store);
static struct r_attribute bias_tee_attribute = __ATTR(bias_tee, 0664, b_show, b_store);
static struct r_attribute fm_notch_attribute = __ATTR(fm_notch, 0664, b_show, b_store);
static struct r_attribute path_attribute = __ATTR(path, 0664, b_show, b_store);
static struct r_attribute rfic_dc_q_attribute = __ATTR(rfic_dc_q, 0664, b_show, b_store);
static struct r_attribute rfic_dc_i_attribute = __ATTR(rfic_dc_i, 0664, b_show, b_store);


static struct attribute *rfnm_rx_def_attrs[] = {
	&freq_attribute.attr,
	&enable_attribute.attr,
	&gain_attribute.attr,
	&apply_attribute.attr,
	&freq_min_attribute.attr,
	&freq_max_attribute.attr,
	&adc_id_attribute.attr,
	&iq_lpf_bw_attribute.attr,
	&agc_attribute.attr,
	&bias_tee_attribute.attr,
	&fm_notch_attribute.attr,
	&path_attribute.attr,
	&rfic_dc_q_attribute.attr,
	&rfic_dc_i_attribute.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};
ATTRIBUTE_GROUPS(rfnm_rx_def);

static struct attribute *rfnm_tx_def_attrs[] = {
	&freq_attribute.attr,
	&enable_attribute.attr,
	&power_attribute.attr,
	&apply_attribute.attr,
	&freq_min_attribute.attr,
	&freq_max_attribute.attr,
	&dac_id_attribute.attr,
	&iq_lpf_bw_attribute.attr,
	&bias_tee_attribute.attr,
	&path_attribute.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};
ATTRIBUTE_GROUPS(rfnm_tx_def);

/*
 * Our own ktype for our kobjects.  Here we specify our sysfs ops, the
 * release function, and the set of default attributes we want created
 * whenever a kobject of this type is registered with the kernel.
 */
static const struct kobj_type rx_ktype = {
	.sysfs_ops = &rfnm_txrx_sysfs_ops,
	.release = foo_release,
	.default_groups = rfnm_rx_def_groups,
};

static const struct kobj_type tx_ktype = {
	.sysfs_ops = &rfnm_txrx_sysfs_ops,
	.release = foo_release,
	.default_groups = rfnm_tx_def_groups,
};




static struct kset *rfnm_dgb_kset[2];
static struct rfnm_ch_obj *ch_obj_list[2][2][8];

static struct rfnm_ch_obj *rfnm_create_ch_obj(int dgb_id, int txrx, int ch)
{
	struct rfnm_ch_obj *foo;
	int retval;

	foo = kzalloc(sizeof(*foo), GFP_KERNEL);
	if (!foo)
		return NULL;

	foo->kobj.kset = rfnm_dgb_kset[dgb_id];

	if(txrx == RFNM_TX) {
		retval = kobject_init_and_add(&foo->kobj, &tx_ktype, NULL, "tx%d", ch);
	} else {
		retval = kobject_init_and_add(&foo->kobj, &rx_ktype, NULL, "rx%d", ch);
	}
	
	if (retval) {
		kobject_put(&foo->kobj);
		return NULL;
	}

	foo->dgb_id = dgb_id;
	foo->dgb_ch_id = ch;
	foo->txrx = txrx; 

	kobject_uevent(&foo->kobj, KOBJ_ADD);

	return foo;
}


void rfnm_dgb_reg(struct rfnm_dgb *dgb_dt) {
	int i;
	int dgb_slot = dgb_dt->dgb_id;
	rfnm_dgb[dgb_slot] = dgb_dt;

	if(dgb_slot == 0) {
		rfnm_dgb_kset[dgb_slot] = kset_create_and_add("rfnm_primary", NULL, kernel_kobj);
	} else {
		rfnm_dgb_kset[dgb_slot] = kset_create_and_add("rfnm_secondary", NULL, kernel_kobj);
	}

	if (!rfnm_dgb_kset[dgb_slot])
		goto ch_reg_error;

	for(i = 0; i < rfnm_dgb[dgb_slot]->tx_ch_cnt; i++) {
		ch_obj_list[dgb_slot][RFNM_TX][i] = rfnm_create_ch_obj(dgb_slot, RFNM_TX, i);
		if (!ch_obj_list[dgb_slot][RFNM_TX][i])
			goto ch_reg_error;
	}


	for(i = 0; i < rfnm_dgb[dgb_slot]->rx_ch_cnt; i++) {
		ch_obj_list[dgb_slot][RFNM_RX][i] = rfnm_create_ch_obj(dgb_slot, RFNM_RX, i);
		if (!ch_obj_list[dgb_slot][RFNM_RX][i])
			goto ch_reg_error;		
	}

	if(dgb_slot == 0) {
		dcs_vmem[HSDAC_CFGCTL1] = (dcs_vmem[HSDAC_CFGCTL1] & 0xfffff0ff) | ((dgb_dt->dac_ifs & 0xfl) << 8);
	}

	uint32_t gpout4 = gpout_vmem[GP_OUT_4];
	
	for(i = 0; i < 2; i++) {
		uint32_t map = (8 * (RFNM_ADC_MAP[i + (dgb_dt->dgb_id << 1)] - 1));
		gpout4 &= ~(0x8 << map);
		if(dgb_dt->dgb_id == 0 && i == 1) {
			if(!dgb_dt->adc_iqswap[i]) {
				gpout4 |= 0x8 << map;
			}
		} else {
			if(dgb_dt->adc_iqswap[i]) {
				gpout4 |= 0x8 << map;
			}
		}
	}

	if(dgb_slot == 0) {
		uint32_t gpout7 = gpout_vmem[GP_OUT_7];
		gpout7 &= ~(0x1 << 3);
		// honestly, what's going on with the transmitter chain???
		if(dgb_dt->dac_iqswap[0]) {
			gpout7 |= 0x1 << 3;
		}
		gpout_vmem[GP_OUT_7] = gpout7;
	}

	gpout_vmem[GP_OUT_4] = gpout4;
	

	return;
ch_reg_error:
	printk("failed to register kset\n");
}
EXPORT_SYMBOL(rfnm_dgb_reg);


void rfnm_dgb_unreg(struct rfnm_dgb *dgb_dt) {

	int dgb_slot = dgb_dt->dgb_id;
	/*rfnm_dgb[dgb_slot]->rx_ch_cnt = 0;
	rfnm_dgb[dgb_slot]->tx_ch_cnt = 0;
	rfnm_dgb[dgb_slot]->board_id = 0;
	rfnm_dgb[dgb_slot]->board_revision_id = 0;
	memset(rfnm_dgb[dgb_slot]->serial_number, 0, 9);*/

	rfnm_dgb[dgb_slot] = NULL;

	kset_unregister(rfnm_dgb_kset[dgb_slot]);

	
}
EXPORT_SYMBOL(rfnm_dgb_unreg);





static __init int rfnm_daughterboard_init(void)
{
	printk("init rfnm_daughterboard\n");

	void __iomem *gpio_iomem;
	gpio_iomem = ioremap(0x00800070, SZ_4K);
	m7_dgb = (void *) gpio_iomem;

	gpio_iomem = ioremap(RFNM_LA_DCS_PHY_ADDR, SZ_16K);
	dcs_vmem = (void *) gpio_iomem;

	gpio_iomem = ioremap(RFNM_LA_GPOUT_PHY_ADDR, SZ_16K);
	gpout_vmem = (void *) gpio_iomem;

	// on boot, enable iq-swap on rx-2/primary (maps to 4)
	gpout_vmem[GP_OUT_4] |= 0x1 << 27;

	bootcfg = memremap(RFNM_BOOTCONFIG_PHYADDR, SZ_4M, MEMREMAP_WB);


	memset(&rfnm_rx_adc_s[0], 0, 4);
	rfnm_tx_dac_s = 0;
		
/*
	struct device *dev;
	int err;

	dev = kmalloc(sizeof(struct device), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	err = device_create_file(dev, &dev_attr_rfnm_ext_ref_out);
	if (err < 0) {
		printk("RFNM: failed to create device file for rfnm_ext_ref_out");
	}

	kfree(dev);
*/

	return 0;
}

static __exit void rfnm_daughterboard_exit(void)
{
	//kobject_put(&foo->kobj);
	//kset_unregister(rfnm_dgb_primary_kset);

	memunmap(bootcfg);
}


MODULE_PARM_DESC(device, "RFNM Granita Daughterboard Driver");
module_init(rfnm_daughterboard_init);
module_exit(rfnm_daughterboard_exit);
MODULE_LICENSE("GPL");
