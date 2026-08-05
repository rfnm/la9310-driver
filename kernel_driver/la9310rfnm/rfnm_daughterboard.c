// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 RFNM

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/dma-mapping.h>
#include <la9310_base.h>
#define RFNM_STATUS_EXT_NO_STRUCT	// version + reject enum only (API types not in scope yet)
#include "rfnm_status_ext.h"
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
#include <linux/i2c.h>

//#include "rfnm_types.h"
#include <linux/rfnm-shared.h>
#undef RFNM_STATUS_EXT_NO_STRUCT
#include "rfnm_status_ext.h"	// second pass: the v4 wire structs (API types now in scope)

struct rfnm_dgb *rfnm_dgb[2];
struct rfnm_bootconfig *bootcfg;
volatile struct rfnm_m7_dgb *m7_dgb;
volatile uint32_t *dcs_vmem;
volatile uint32_t *gpout_vmem;

uint8_t rfnm_rx_adc_s[4];
uint8_t rfnm_tx_dac_s;

int abs_ch_cnt_tx = 0;
int abs_ch_cnt_rx = 0;

struct i2c_client *si5510_i2c_client;
struct device *si5510_i2c_dev;

uint64_t rfnm_user_samp_rate_hz;

extern void rfnm_register_reset_dgb_cb(void (*cb)(void));





void rfnm_dgb_reg_rx_ch(struct rfnm_dgb *dgb_dt, struct rfnm_api_rx_ch * rx_ch, struct rfnm_api_rx_ch * rx_s) {
	int dgb_slot = dgb_dt->dgb_id;
	rfnm_dgb[dgb_slot] = dgb_dt;
	rx_ch->dgb_id = dgb_slot;
	rx_ch->dgb_ch_id = rfnm_dgb[dgb_slot]->rx_ch_cnt;
	rx_ch->abs_id = abs_ch_cnt_rx++;
	rx_ch->adc_id += dgb_slot * 2;
	rx_ch->avail = 1;
	rfnm_dgb[dgb_slot]->rx_ch[rx_ch->dgb_ch_id] = rx_ch;
	rfnm_dgb[dgb_slot]->rx_s[rx_ch->dgb_ch_id] = rx_s;
	rfnm_dgb[dgb_slot]->rx_ch_cnt++;

	// re-assign abs_id
	int i, q, d = 0;
	for(i = 0; i < 2; i++) {
		if(!rfnm_dgb[i]) {
			continue;
		}
		for(q = 0; q < rfnm_dgb[i]->rx_ch_cnt; q++) {
			rfnm_dgb[i]->rx_ch[q]->abs_id = d++;
		}
	}

#if 0
	printk("rx abs_id %d dgb_ch_id %d dgb_id %d adc_id %d\n",
				rfnm_dgb[dgb_slot]->rx_ch[rx_ch->dgb_ch_id]->abs_id, 
				rfnm_dgb[dgb_slot]->rx_ch[rx_ch->dgb_ch_id]->dgb_ch_id, 
				rfnm_dgb[dgb_slot]->rx_ch[rx_ch->dgb_ch_id]->dgb_id, 
				rfnm_dgb[dgb_slot]->rx_ch[rx_ch->dgb_ch_id]->adc_id);
#endif
}
EXPORT_SYMBOL(rfnm_dgb_reg_rx_ch);

void rfnm_dgb_reg_tx_ch(struct rfnm_dgb *dgb_dt, struct rfnm_api_tx_ch * tx_ch, struct rfnm_api_tx_ch * tx_s) {
	int dgb_slot = dgb_dt->dgb_id;
	// slot-1 TX registration unlocked 2026-07-12 (was a hard slot-0 gate): the RF side of a
	// secondary-slot TX channel is fully driveable (yucca chip-level TX proven); the DAC DATA
	// path is still slot-0-only (GP_OUT_7 iqswap + the single TX pump both assume dac 0), so
	// secondary TX channels are RF-control-only until the dual-DAC plumbing lands
	rfnm_dgb[dgb_slot] = dgb_dt;
	tx_ch->dgb_id = dgb_slot;
	tx_ch->dgb_ch_id = rfnm_dgb[dgb_slot]->tx_ch_cnt;
	//tx_ch->abs_id = abs_ch_cnt_tx++;
	tx_ch->avail = 1;
	rfnm_dgb[dgb_slot]->tx_ch[tx_ch->dgb_ch_id] = tx_ch;
	rfnm_dgb[dgb_slot]->tx_s[tx_ch->dgb_ch_id] = tx_s;
	rfnm_dgb[dgb_slot]->tx_ch_cnt++;

	// re-assign abs_id
	int i, q, d = 0;
	for(i = 0; i < 2; i++) {
		if(!rfnm_dgb[i]) {
			continue;
		}
		for(q = 0; q < rfnm_dgb[i]->tx_ch_cnt; q++) {
			rfnm_dgb[i]->tx_ch[q]->abs_id = d++;
		}
	}

#if 0
	printk("tx abs_id %d dgb_ch_id %d dgb_id %d dac_id %d\n",
				rfnm_dgb[dgb_slot]->tx_ch[tx_ch->dgb_ch_id]->abs_id, 
				rfnm_dgb[dgb_slot]->tx_ch[tx_ch->dgb_ch_id]->dgb_ch_id, 
				rfnm_dgb[dgb_slot]->tx_ch[tx_ch->dgb_ch_id]->dgb_id, 
				rfnm_dgb[dgb_slot]->tx_ch[tx_ch->dgb_ch_id]->dac_id);
#endif
}
EXPORT_SYMBOL(rfnm_dgb_reg_tx_ch);


void rfnm_populate_dev_hwinfo(struct rfnm_dev_hwinfo * r_hwinfo) {
	int i;

	memset(r_hwinfo, 0, sizeof(struct rfnm_dev_hwinfo));

	// v3: the wire surface (dev_status ext tail) is module-owned, so the version is
	// too - the kernel-tree RFNM_PROTOCOL_VERSION stays 2 (bumping it would touch the
	// fleet Image via kernel.release/vermagic). Old librfnm pairs fail LOUDLY at
	// discovery/open ("SW_UPGRADE_REQUIRED"), which is the deploy-together guard.
	r_hwinfo->protocol_version = RFNM_PROTOCOL_VERSION_EXT;

	r_hwinfo->clock.dcs_clk = rfnm_si5510_get_dcs_freq(si5510_i2c_client);
	rfnm_la9310_get_clock_state(&r_hwinfo->clock.rx_dcs_div, &r_hwinfo->clock.tx_dcs_div,
			&r_hwinfo->clock.rx_decim_log2, &r_hwinfo->clock.tx_interp_log2);
	r_hwinfo->clock.samp_rate = rfnm_user_samp_rate_hz;
	r_hwinfo->clock.samp_rate_min = 195375;	// floor of the unified ladder: rate x 512 (hw-bit + VSPA 256x) must reach the
						// 100 MHz DCS minimum (lower DCS clocks hang the board - a 61.44 MHz DCS froze PCIe)
						// AND the DCS target must be kHz-aligned for the si5510: at the x512
						// rung that makes user rates multiples of 125 Hz - first valid rate >= the DCS
						// floor is 195375 (195313 itself plans DCS 100000256 Hz, not synthesizable)
	r_hwinfo->clock.samp_rate_max = 160e6; // streaming is PCIe-bandwidth bound (~160 MSPS); the DCS itself can overclock to 200 MHz for oversampling
	r_hwinfo->clock.samp_rate_step = 1000;
	
	
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
// v4 side-store: which field each rejection above was about (same indexing as the
// ecodes; lives beside the base struct so the kernel-tree layout stays untouched)
static struct {
	uint8_t tx[8];
	uint8_t rx[8];
	uint8_t samp_rate;
} rfnm_dev_work_rej;
// v5 side-store: the apply timing handle. Each apply work clears ITS direction bit and snapshots ITS
// wire epoch BEFORE the stream call, then re-arms bits from the send/reclock counter
// deltas after. Invariant the client settle test rests on: bit set => the snapshot
// paired with it predates the send, so "epoch advanced past snapshot" terminates; a
// stale bit/snapshot pair from an earlier apply is already-settled by construction.
// Works serialize on rfnm_chlist_apply_lock, so the diffs never interleave.
extern uint32_t rfnm_stream_send_cnt;	// rfnm_lalib: real (non-deduped) stream sends
extern uint32_t rfnm_dcs_reclock_cnt;	// rfnm_lalib: si5510 dcs reclocks (hard-reset class)
void rfnm_get_wire_epochs(uint32_t *rx_e, uint32_t *tx_e);	// la9310rfnm
static struct {
	uint8_t brk;		// bit0 rx break, bit1 tx break, bit2 dcs_freq reclock
	uint32_t rx_epoch;	// wire epochs at apply processing (pre-stream snapshot)
	uint32_t tx_epoch;
} rfnm_dev_work_timing;

void rfnm_populate_dev_set_res(struct rfnm_dev_get_set_result * r_res) {
	memcpy(r_res, &rfnm_dev_work_res, sizeof(struct rfnm_dev_get_set_result));
}
EXPORT_SYMBOL(rfnm_populate_dev_set_res);

void rfnm_populate_dev_set_res_ext(struct rfnm_dev_get_set_result_ext *r) {
	memcpy(&r->base, &rfnm_dev_work_res, sizeof(struct rfnm_dev_get_set_result));
	memcpy(r->tx_reject_field, rfnm_dev_work_rej.tx, sizeof(r->tx_reject_field));
	memcpy(r->rx_reject_field, rfnm_dev_work_rej.rx, sizeof(r->rx_reject_field));
	r->samp_rate_reject_field = rfnm_dev_work_rej.samp_rate;
	r->timing_break = rfnm_dev_work_timing.brk;
	r->rx_epoch_at_apply = rfnm_dev_work_timing.rx_epoch;
	r->tx_epoch_at_apply = rfnm_dev_work_timing.tx_epoch;
}
EXPORT_SYMBOL(rfnm_populate_dev_set_res_ext);

int rfnm_set_samp_rate_user(uint64_t freq, uint32_t cc) {
	extern int rfnm_phy_gen_session_ok(void);
	extern int rfnm_bringup_complete;

	if(!READ_ONCE(rfnm_bringup_complete)) {
		// bring-up gate: a rate set mid-bring-up reclocks the DCS and hard-resets the
		// LA9310 under a half-initialized daughterboard stack - refuse honestly
		pr_warn_ratelimited("RFNM: samp-rate set refused, radio bring-up incomplete\n");
		rfnm_dev_work_res.cc_samp_rate = cc;
		rfnm_dev_work_res.samp_rate_ecode = RFNM_API_PROBE_FAIL;
		rfnm_dev_work_rej.samp_rate = RFNM_REJ_BOOT;
		return -EAGAIN;
	}
	if(!rfnm_phy_gen_session_ok()) {
		return -ENODEV;	// stale time generation - reopen before reconfiguring
	}
	struct rfnm_dev_hwinfo hwinfo;
	int ret = 0;

	rfnm_populate_dev_hwinfo(&hwinfo);

	if(freq < hwinfo.clock.samp_rate_min || freq > hwinfo.clock.samp_rate_max) {
		printk("RFNM: rejected invalid sample rate %llu Hz\n", (unsigned long long)freq);
		ret = -EINVAL;
	} else if(!rfnm_la9310_samp_rate_ok(freq)) {
		// The min/max check alone let set_samp_rate() stamp OK on a rate whose DCS plan
		// cannot actually be served, and hwinfo then reported the request as fact while the
		// hardware delivered something else. Validate the plan (existence + exact si5510
		// synthesizability) at set time and answer NOT_SUPPORTED honestly instead.
		printk("RFNM: rejected sample rate %llu Hz: no synthesizable DCS plan\n", (unsigned long long)freq);
		ret = -EINVAL;
	} else {
		rfnm_user_samp_rate_hz = freq;
		printk("RFNM: set sample rate to %llu Hz\n", (unsigned long long)rfnm_user_samp_rate_hz);
	}

	// Always stamp the result for this command (cc) with its status; the host polls
	// GET_SET_RESULT for the cc match and reads samp_rate_ecode, like apply()'s ecodes.
	rfnm_dev_work_res.cc_samp_rate = cc;
	rfnm_dev_work_res.samp_rate_ecode = ret ? RFNM_API_NOT_SUPPORTED : RFNM_API_OK;
	rfnm_dev_work_rej.samp_rate = ret ? RFNM_REJ_RATE : RFNM_REJ_NONE;
	return ret;
}
EXPORT_SYMBOL(rfnm_set_samp_rate_user);

// Idle TX park. Session teardown is an SM reset - no client ever sends
// TX RF_OFF, so a departed client's TX synthesizer keeps running at its last tune and
// the carrier leaks onto the air forever (reboot was the only cure). In TDD the UL
// tune equals the cell frequency, so the next RX session on the cell reads the parked
// carrier as a huge DC + phase-noise floor (+10-16 dB) + ~6 dB compression: the
// "RX degrades after the board's own TX cycles" face. Registered on the la9310rfnm
// stand-down hook (fires when NO transport shows a live consumer); runs from a work
// item because the dgb drivers' RF_OFF legs do ms-class SPI under their apply locks.
// tx_idle_park=0 restores the legacy leave-the-synth-running behavior.
static int tx_idle_park = 1;
module_param(tx_idle_park, int, 0644);
MODULE_PARM_DESC(tx_idle_park, "RF_OFF armed TX channels when the radio goes idle (default 1; 0 = legacy)");

extern void (*rfnm_tx_idle_park_cb)(void);
int rfnm_dgb_tx_set(struct rfnm_dgb *rfnm_dgb_dt, struct rfnm_api_tx_ch * tx_ch);

static void rfnm_dgb_tx_idle_park_work(struct work_struct *w) {
	int slot, ch, parked = 0;

	for(slot = 0; slot < 2; slot++) {
		struct rfnm_dgb *dgb = rfnm_dgb[slot];

		if(!dgb || !dgb->tx_ch_set) {
			continue;
		}
		for(ch = 0; ch < dgb->tx_ch_cnt && ch < 4; ch++) {
			if(!dgb->tx_ch[ch] || dgb->tx_ch[ch]->enable == RFNM_CH_RF_OFF) {
				continue;
			}
			dgb->tx_ch[ch]->enable = RFNM_CH_RF_OFF;
			if(!rfnm_dgb_tx_set(dgb, dgb->tx_ch[ch])) {
				parked++;
			}
		}
	}
	if(parked) {
		printk("RFNM: tx idle park: %d channel(s) RF_OFF (radio idle, synth carrier off the air)\n", parked);
	}
}
static DECLARE_WORK(rfnm_dgb_tx_idle_park_w, rfnm_dgb_tx_idle_park_work);

static void rfnm_dgb_tx_idle_park(void) {
	if(tx_idle_park) {
		schedule_work(&rfnm_dgb_tx_idle_park_w);
	}
}

int rfnm_dgb_tx_set(struct rfnm_dgb *rfnm_dgb_dt, struct rfnm_api_tx_ch * tx_ch) {
	int (*ch_fun)(struct rfnm_dgb *, struct rfnm_api_tx_ch *);
	ch_fun = rfnm_dgb_dt->tx_ch_set;
	int r = ch_fun(rfnm_dgb_dt, tx_ch);

	if(!r) {
		if(tx_ch->stream == RFNM_CH_STREAM_AUTO) {
			if(tx_ch->enable != RFNM_CH_RF_OFF) {
				rfnm_tx_dac_s = 1;
			} else {
				rfnm_tx_dac_s = 0;
			}
		} else {
			if(tx_ch->stream == RFNM_CH_STREAM_ON) {
				rfnm_tx_dac_s = 1;
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
		if(rx_ch->stream == RFNM_CH_STREAM_AUTO) {
			if(rx_ch->enable != RFNM_CH_RF_OFF) {
				rfnm_rx_adc_s[rx_ch->adc_id] = 1;
			} else {
				rfnm_rx_adc_s[rx_ch->adc_id] = 0;
			}
		} else {
			if(rx_ch->stream == RFNM_CH_STREAM_ON) {
				rfnm_rx_adc_s[rx_ch->adc_id] = 1;
			} else if(rx_ch->stream == RFNM_CH_STREAM_OFF) {
				rfnm_rx_adc_s[rx_ch->adc_id] = 0;
			}
		}
	}

	return r;
}

// AGC entry points (rfnm_agc.ko). Gain steps ride the NORMAL rx_ch_set path - the dgb
// drivers keep it delta-optimized so a gain-only restep touches just the gain stages - and
// the applied gain lands in rx_ch/rx_s, so GET_RX_CH_LIST reports the live AGC-chosen gain.
struct rfnm_dgb * rfnm_dgb_get(int dgb_id) {
	if(dgb_id < 0 || dgb_id > 1) {
		return NULL;
	}
	return rfnm_dgb[dgb_id];
}
EXPORT_SYMBOL(rfnm_dgb_get);

int rfnm_dgb_rx_set_gain(int dgb_id, int ch_id, int gain_db) {
	struct rfnm_dgb *dgb_dt = rfnm_dgb_get(dgb_id);
	struct rfnm_api_rx_ch *rx_ch;

	if(!dgb_dt || ch_id < 0 || ch_id >= dgb_dt->rx_ch_cnt || !dgb_dt->rx_ch[ch_id]) {
		return -ENODEV;
	}
	rx_ch = dgb_dt->rx_ch[ch_id];
	if(gain_db < rx_ch->gain_range.min) {
		gain_db = rx_ch->gain_range.min;
	}
	if(gain_db > rx_ch->gain_range.max) {
		gain_db = rx_ch->gain_range.max;
	}
	rx_ch->gain = gain_db;
	return rfnm_dgb_rx_set(dgb_dt, rx_ch);
}
EXPORT_SYMBOL(rfnm_dgb_rx_set_gain);

/* Generic wire-field DC setter for in-kernel measured loops (rfnm_qec): writes the ONE
 * public RFIC IQ correction (rfic_dc_i/q - the same client-owned wire field) into the
 * channel struct and runs the normal apply, the exact pattern of rfnm_dgb_rx_set_gain.
 * No chip vocabulary at this layer: how the daughterboard realizes the correction (one
 * knob, several, cross-coupled) is its driver's internal business. GET echoes the live
 * values, so every client sees one source of truth. */
int rfnm_dgb_rx_set_dc(int dgb_id, int ch_id, int dc_i, int dc_q) {
	struct rfnm_dgb *dgb_dt = rfnm_dgb_get(dgb_id);

	if(!dgb_dt || ch_id < 0 || ch_id >= dgb_dt->rx_ch_cnt) {
		return -ENODEV;
	}
	if(dc_i < -126 || dc_i > 126 || dc_q < -126 || dc_q > 126) {
		return -EINVAL;
	}
	dgb_dt->rx_ch[ch_id]->rfic_dc_i = dc_i;
	dgb_dt->rx_ch[ch_id]->rfic_dc_q = dc_q;
	return rfnm_dgb_rx_set(dgb_dt, dgb_dt->rx_ch[ch_id]);
}
EXPORT_SYMBOL(rfnm_dgb_rx_set_dc);

struct rfnm_dev_tx_ch_list r_tx_chlist_work;
struct rfnm_dev_rx_ch_list r_rx_chlist_work;

// The tx and rx chlist works both end in a full rfnm_la9310_stream() reconfig. They run
// on the multi-threaded system workqueue, so a combined apply (librfnm sends the tx and
// rx lists back-to-back) had the second stream call racing the first mid-reconfig and
// failing with EBUSY. One mutex over both bodies: the second work then computes its
// stream config from the merged flags and wins cleanly.
static DEFINE_MUTEX(rfnm_chlist_apply_lock);

#include <linux/workqueue.h>

static bool rfnm_ch_enable_valid(enum rfnm_ch_enable enable) {
	return enable >= RFNM_CH_RF_OFF && enable <= RFNM_CH_RF_ON_TDD;
}

static bool rfnm_ch_stream_valid(enum rfnm_ch_stream stream) {
	return stream >= RFNM_CH_STREAM_AUTO && stream <= RFNM_CH_STREAM_ON;
}

static bool rfnm_bias_tee_valid(enum rfnm_bias_tee bias_tee) {
	return bias_tee >= RFNM_BIAS_TEE_OFF && bias_tee <= RFNM_BIAS_TEE_ON;
}

static bool rfnm_agc_valid(enum rfnm_agc_type agc) {
	return agc >= RFNM_AGC_OFF && agc <= RFNM_AGC_DEFAULT;
}

static bool rfnm_fm_notch_valid(enum rfnm_fm_notch fm_notch) {
	return fm_notch >= RFNM_FM_NOTCH_AUTO && fm_notch <= RFNM_FM_NOTCH_OFF;
}

static int rfnm_validate_tx_ch(const struct rfnm_api_tx_ch *tx_ch, uint8_t *rej) {
	if(tx_ch->freq < tx_ch->freq_min || tx_ch->freq > tx_ch->freq_max) {
		*rej = RFNM_REJ_FREQ;
		return RFNM_API_NOT_SUPPORTED;
	}
	if(tx_ch->rfic_lpf_bw < 0) {
		*rej = RFNM_REJ_LPF_BW;
		return RFNM_API_NOT_SUPPORTED;
	}
	if(tx_ch->power < tx_ch->power_range.min || tx_ch->power > tx_ch->power_range.max) {
		*rej = RFNM_REJ_POWER;
		return RFNM_API_NOT_SUPPORTED;
	}
	if(!rfnm_ch_enable_valid(tx_ch->enable)) {
		*rej = RFNM_REJ_ENABLE;
		return RFNM_API_NOT_SUPPORTED;
	}
	if(!rfnm_ch_stream_valid(tx_ch->stream)) {
		*rej = RFNM_REJ_STREAM;
		return RFNM_API_NOT_SUPPORTED;
	}
	if(!rfnm_bias_tee_valid(tx_ch->bias_tee)) {
		*rej = RFNM_REJ_BIAS_TEE;
		return RFNM_API_NOT_SUPPORTED;
	}

	*rej = RFNM_REJ_NONE;
	return RFNM_API_OK;
}

static int rfnm_validate_rx_ch(const struct rfnm_api_rx_ch *rx_ch, uint8_t *rej) {
	if(rx_ch->freq < rx_ch->freq_min || rx_ch->freq > rx_ch->freq_max) {
		*rej = RFNM_REJ_FREQ;
		return RFNM_API_NOT_SUPPORTED;
	}
	if(rx_ch->rfic_lpf_bw < 0) {
		*rej = RFNM_REJ_LPF_BW;
		return RFNM_API_NOT_SUPPORTED;
	}
	if(rx_ch->gain < rx_ch->gain_range.min || rx_ch->gain > rx_ch->gain_range.max) {
		*rej = RFNM_REJ_GAIN;
		return RFNM_API_NOT_SUPPORTED;
	}
	if(rx_ch->rfic_dc_q < -126 || rx_ch->rfic_dc_q > 126 || rx_ch->rfic_dc_i < -126 || rx_ch->rfic_dc_i > 126) {
		/* logical codes spanning the full silicon authority (2026-07-21: two stacked
		 * +-63 knobs on Lime; other boards clamp internally as they see fit) */
		*rej = RFNM_REJ_DC_TRIM;
		return RFNM_API_NOT_SUPPORTED;
	}
	if(!rfnm_ch_enable_valid(rx_ch->enable)) {
		*rej = RFNM_REJ_ENABLE;
		return RFNM_API_NOT_SUPPORTED;
	}
	if(!rfnm_ch_stream_valid(rx_ch->stream)) {
		*rej = RFNM_REJ_STREAM;
		return RFNM_API_NOT_SUPPORTED;
	}
	if(!rfnm_agc_valid(rx_ch->agc)) {
		*rej = RFNM_REJ_AGC;
		return RFNM_API_NOT_SUPPORTED;
	}
	if(!rfnm_bias_tee_valid(rx_ch->bias_tee)) {
		*rej = RFNM_REJ_BIAS_TEE;
		return RFNM_API_NOT_SUPPORTED;
	}
	if(!rfnm_fm_notch_valid(rx_ch->fm_notch)) {
		*rej = RFNM_REJ_FM_NOTCH;
		return RFNM_API_NOT_SUPPORTED;
	}

	*rej = RFNM_REJ_NONE;
	return RFNM_API_OK;
}

static void rfnm_mark_missing_ch(uint8_t requested, uint8_t present, int32_t ecodes[8], uint8_t rej[8]) {
	int i;
	uint8_t missing = requested & ~present;

	for(i = 0; i < 8; i++) {
		if(missing & (1U << i)) {
			ecodes[i] = RFNM_API_NOT_SUPPORTED;
			rej[i] = RFNM_REJ_CH_MISSING;
		}
	}
}

static void rfnm_apply_dev_tx_chlist_work_locked(struct work_struct * tasklet_data);
void rfnm_apply_dev_tx_chlist_work(struct work_struct * tasklet_data) {
	mutex_lock(&rfnm_chlist_apply_lock);
	rfnm_apply_dev_tx_chlist_work_locked(tasklet_data);
	mutex_unlock(&rfnm_chlist_apply_lock);
}
static void rfnm_apply_dev_tx_chlist_work_locked(struct work_struct * tasklet_data) {
	int i, q, d = 0;
	int stream_ret;
	int stream_api_error;
	uint8_t present = 0;

	memset(rfnm_dev_work_rej.tx, 0, sizeof(rfnm_dev_work_rej.tx));
	stream_ret = rfnm_wait_restart_sm_idle(15000);
	if(stream_ret) {
		stream_api_error = (stream_ret == -ETIMEDOUT) ? RFNM_API_TIMEOUT : RFNM_API_PROBE_FAIL;
		for(i = 0; i < 8; i++) {
			rfnm_dev_work_res.tx_ecodes[i] = stream_api_error;
		}
		rfnm_dev_work_res.cc_tx = r_tx_chlist_work.cc;
		return;
	}

	//printk("inside rfnm_apply_dev_tx_chlist\n");
	memset(rfnm_dev_work_res.tx_ecodes, 0, sizeof(rfnm_dev_work_res.tx_ecodes));

	for(i = 0; i < 2; i++) {
		if(!rfnm_dgb[i]) {
			//printk("dgb %d not inserted\n", i);
			continue;
		}
		for(q = 0; q < rfnm_dgb[i]->tx_ch_cnt; q++) {
			int abs_id = rfnm_dgb[i]->tx_ch[q]->abs_id;
			uint8_t ch_bit = 1U << abs_id;

			present |= ch_bit;
			if(r_tx_chlist_work.apply & ch_bit) {
				int ecode = rfnm_validate_tx_ch(&r_tx_chlist_work.ch[d], &rfnm_dev_work_rej.tx[abs_id]);
				if(ecode) {
					rfnm_dev_work_res.tx_ecodes[abs_id] = ecode;
					d++;
					continue;
				}
			}

			memcpy(rfnm_dgb[i]->tx_ch[q], &r_tx_chlist_work.ch[d], sizeof(struct rfnm_api_tx_ch));
			if(r_tx_chlist_work.apply & ch_bit) {
				int ecode = rfnm_dgb_tx_set(rfnm_dgb[i], rfnm_dgb[i]->tx_ch[q]);
				rfnm_dev_work_res.tx_ecodes[abs_id] = -ecode;
				rfnm_dev_work_rej.tx[abs_id] = ecode ? RFNM_REJ_DEVICE : RFNM_REJ_NONE;
			}
			d++;
		}
	}

	rfnm_mark_missing_ch(r_tx_chlist_work.apply, present, rfnm_dev_work_res.tx_ecodes, rfnm_dev_work_rej.tx);

	{
		uint32_t snd0 = rfnm_stream_send_cnt, rcl0 = rfnm_dcs_reclock_cnt, e_rx, e_tx;
		rfnm_get_wire_epochs(&e_rx, &e_tx);
		rfnm_dev_work_timing.brk &= ~0x2;
		rfnm_dev_work_timing.tx_epoch = e_tx;
		stream_ret = rfnm_la9310_stream(rfnm_user_samp_rate_hz, rfnm_tx_dac_s, rfnm_rx_adc_s);
		if(rfnm_stream_send_cnt != snd0) {
			// a real send re-gates the whole chain: every direction ACTIVE after this
			// apply re-anchors. The cross-direction (rx) bit rides the OLDER rx
			// snapshot - safe, the send postdates it (see invariant at the side-store).
			rfnm_dev_work_timing.brk |= (rfnm_tx_dac_s ? 0x2 : 0)
					| ((rfnm_rx_adc_s[0] || rfnm_rx_adc_s[1] || rfnm_rx_adc_s[2] || rfnm_rx_adc_s[3]) ? 0x1 : 0);
		}
		if(rfnm_dcs_reclock_cnt != rcl0) {
			rfnm_dev_work_timing.brk |= 0x4;
		}
	}
	if(stream_ret) {
		stream_api_error = (stream_ret == -ETIMEDOUT) ? RFNM_API_TIMEOUT : RFNM_API_PROBE_FAIL;
		for(i = 0; i < 8; i++) {
			if(!rfnm_dev_work_res.tx_ecodes[i]) {
				rfnm_dev_work_res.tx_ecodes[i] = stream_api_error;
				rfnm_dev_work_rej.tx[i] = RFNM_REJ_RATE;
			}
		}
	}

	rfnm_dev_work_res.cc_tx = r_tx_chlist_work.cc;
}
DECLARE_WORK(rfnm_tx_chlist_work, &rfnm_apply_dev_tx_chlist_work);





static void rfnm_apply_dev_rx_chlist_work_locked(struct work_struct * tasklet_data);
void rfnm_apply_dev_rx_chlist_work(struct work_struct * tasklet_data) {
	mutex_lock(&rfnm_chlist_apply_lock);
	rfnm_apply_dev_rx_chlist_work_locked(tasklet_data);
	mutex_unlock(&rfnm_chlist_apply_lock);
}
static void rfnm_apply_dev_rx_chlist_work_locked(struct work_struct * tasklet_data) {
	int i, q, d = 0;
	int stream_ret;
	int stream_api_error;
	uint8_t present = 0;

	memset(rfnm_dev_work_rej.rx, 0, sizeof(rfnm_dev_work_rej.rx));
	stream_ret = rfnm_wait_restart_sm_idle(15000);
	if(stream_ret) {
		stream_api_error = (stream_ret == -ETIMEDOUT) ? RFNM_API_TIMEOUT : RFNM_API_PROBE_FAIL;
		for(i = 0; i < 8; i++) {
			rfnm_dev_work_res.rx_ecodes[i] = stream_api_error;
		}
		rfnm_dev_work_res.cc_rx = r_rx_chlist_work.cc;
		return;
	}

	memset(rfnm_dev_work_res.rx_ecodes, 0, sizeof(rfnm_dev_work_res.rx_ecodes));

	for(i = 0; i < 2; i++) {
		if(!rfnm_dgb[i]) {
			continue;
		}
		for(q = 0; q < rfnm_dgb[i]->rx_ch_cnt; q++) {
			int abs_id = rfnm_dgb[i]->rx_ch[q]->abs_id;
			uint8_t ch_bit = 1U << abs_id;

			present |= ch_bit;
			if(r_rx_chlist_work.apply & ch_bit) {
				int ecode = rfnm_validate_rx_ch(&r_rx_chlist_work.ch[d], &rfnm_dev_work_rej.rx[abs_id]);
				if(ecode) {
					rfnm_dev_work_res.rx_ecodes[abs_id] = ecode;
					d++;
					continue;
				}
			}

			// AGC gain ownership: while a channel runs AGC, the gain field is
			// read-only telemetry - the kernel AGC loop owns it and mirrors its live
			// value into rx_ch->gain (rfnm_dgb_rx_set_gain). A client apply carrying a
			// stale gain (typical: re-apply for a frequency change) must not yank the
			// gain out from under the loop, so the incoming value is replaced with the
			// live one. Switching agc OFF in the same apply hands control back with
			// whatever gain the client wrote.
			if(r_rx_chlist_work.ch[d].agc != RFNM_AGC_OFF) {
				r_rx_chlist_work.ch[d].gain = rfnm_dgb[i]->rx_ch[q]->gain;
			}
			memcpy(rfnm_dgb[i]->rx_ch[q], &r_rx_chlist_work.ch[d], sizeof(struct rfnm_api_rx_ch));
			if(r_rx_chlist_work.apply & ch_bit) {
				int ecode = rfnm_dgb_rx_set(rfnm_dgb[i], rfnm_dgb[i]->rx_ch[q]);
				rfnm_dev_work_res.rx_ecodes[abs_id] = -ecode;
				rfnm_dev_work_rej.rx[abs_id] = ecode ? RFNM_REJ_DEVICE : RFNM_REJ_NONE;
			}
			d++;
		}
	}

	rfnm_mark_missing_ch(r_rx_chlist_work.apply, present, rfnm_dev_work_res.rx_ecodes, rfnm_dev_work_rej.rx);

	{
		uint32_t snd0 = rfnm_stream_send_cnt, rcl0 = rfnm_dcs_reclock_cnt, e_rx, e_tx;
		rfnm_get_wire_epochs(&e_rx, &e_tx);
		rfnm_dev_work_timing.brk &= ~0x1;
		rfnm_dev_work_timing.rx_epoch = e_rx;
		stream_ret = rfnm_la9310_stream(rfnm_user_samp_rate_hz, rfnm_tx_dac_s, rfnm_rx_adc_s);
		if(rfnm_stream_send_cnt != snd0) {
			// mirror of the tx work: bits for every direction active after this apply;
			// the cross-direction (tx) bit rides the older tx snapshot - safe (invariant)
			rfnm_dev_work_timing.brk |= ((rfnm_rx_adc_s[0] || rfnm_rx_adc_s[1] || rfnm_rx_adc_s[2] || rfnm_rx_adc_s[3]) ? 0x1 : 0)
					| (rfnm_tx_dac_s ? 0x2 : 0);
		}
		if(rfnm_dcs_reclock_cnt != rcl0) {
			rfnm_dev_work_timing.brk |= 0x4;
		}
	}
	if(stream_ret) {
		stream_api_error = (stream_ret == -ETIMEDOUT) ? RFNM_API_TIMEOUT : RFNM_API_PROBE_FAIL;
		for(i = 0; i < 8; i++) {
			if(!rfnm_dev_work_res.rx_ecodes[i]) {
				rfnm_dev_work_res.rx_ecodes[i] = stream_api_error;
				rfnm_dev_work_rej.rx[i] = RFNM_REJ_RATE;
			}
		}
	}
//	rfnm_la9310_stream(rfnm_tx_dac_s, rfnm_rx_adc_s);
	rfnm_dev_work_res.cc_rx = r_rx_chlist_work.cc;
}

DECLARE_WORK(rfnm_rx_chlist_work , &rfnm_apply_dev_rx_chlist_work);

void rfnm_apply_dev_tx_chlist(struct rfnm_dev_tx_ch_list * r_chlist) {
	extern int rfnm_phy_gen_session_ok(void);
	extern int rfnm_bringup_complete;

	if(!READ_ONCE(rfnm_bringup_complete)) {
		int i;
		pr_warn_ratelimited("RFNM: tx apply refused, radio bring-up incomplete\n");
		for(i = 0; i < 8; i++) {
			if(r_chlist->apply & (1U << i)) {
				rfnm_dev_work_res.tx_ecodes[i] = RFNM_API_PROBE_FAIL;
				rfnm_dev_work_rej.tx[i] = RFNM_REJ_BOOT;
			}
		}
		rfnm_dev_work_res.cc_tx = r_chlist->cc;
		return;
	}
	if(!rfnm_phy_gen_session_ok()) {
		return;	// apply dropped (fire-and-forget path; refusal is dmesg-visible)
	}
	cancel_work_sync(&rfnm_tx_chlist_work);
	memcpy(&r_tx_chlist_work, r_chlist, sizeof(struct rfnm_dev_tx_ch_list));
	schedule_work(&rfnm_tx_chlist_work);
}
EXPORT_SYMBOL(rfnm_apply_dev_tx_chlist);

void rfnm_apply_dev_rx_chlist(struct rfnm_dev_rx_ch_list * r_chlist) {
	extern int rfnm_phy_gen_session_ok(void);
	extern int rfnm_bringup_complete;

	if(!READ_ONCE(rfnm_bringup_complete)) {
		int i;
		pr_warn_ratelimited("RFNM: rx apply refused, radio bring-up incomplete\n");
		for(i = 0; i < 8; i++) {
			if(r_chlist->apply & (1U << i)) {
				rfnm_dev_work_res.rx_ecodes[i] = RFNM_API_PROBE_FAIL;
				rfnm_dev_work_rej.rx[i] = RFNM_REJ_BOOT;
			}
		}
		rfnm_dev_work_res.cc_rx = r_chlist->cc;
		return;
	}
	if(!rfnm_phy_gen_session_ok()) {
		return;	// apply dropped (fire-and-forget path; refusal is dmesg-visible)
	}
	// same ep0-atomic rule as the tx variant: no sleeping cancel here
	memcpy(&r_rx_chlist_work, r_chlist, sizeof(struct rfnm_dev_rx_ch_list));
	schedule_work(&rfnm_rx_chlist_work);
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

	// From this moment the M7 drives the physical latches from its own
	// fe_tdd copy - the kernel shadow no longer reflects hardware. Invalidate it
	// so whichever apply next re-owns the port re-drives every latch instead of
	// trusting a stale mirror. (Serialized: we run inside the apply that holds
	// the dgb apply lock.)
	memset(&dgb_dt->fe.latch_val_last_written, 0xff,
			sizeof(dgb_dt->fe.latch_val_last_written));
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

	if(strcmp(attr->attr.name, "rfic_lpf_bw") == 0) {
		if(ch_obj->txrx == RFNM_RX) {
			return sysfs_emit(buf, "%d\n", rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->rfic_lpf_bw);
		} else {
			return sysfs_emit(buf, "%d\n", rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->rfic_lpf_bw);
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

		if(enable == RFNM_CH_RF_OFF) {
			return sysfs_emit(buf, "off\n");
		} else if(enable == RFNM_CH_RF_ON) {
			return sysfs_emit(buf, "on\n");
		} else if(enable == RFNM_CH_RF_ON_TDD) {
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

	if(strcmp(attr->attr.name, "rfic_lpf_bw") == 0) {
		if(intconv < 0) {
			return -EINVAL;
		}
		
		if(ch_obj->txrx == RFNM_RX) {
			rfnm_dgb[ch_obj->dgb_id]->rx_ch[ch_obj->dgb_ch_id]->rfic_lpf_bw = var;
		} else {
			rfnm_dgb[ch_obj->dgb_id]->tx_ch[ch_obj->dgb_ch_id]->rfic_lpf_bw = var;
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
			enable = RFNM_CH_RF_OFF;
		} else if(strcmp(buf_red, "on") == 0) {
			enable = RFNM_CH_RF_ON;
		} else if(strcmp(buf_red, "tdd") == 0) {
			enable = RFNM_CH_RF_ON_TDD;
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
static struct r_attribute rfic_lpf_bw_attribute = __ATTR(rfic_lpf_bw, 0664, b_show, b_store);
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
	&rfic_lpf_bw_attribute.attr,
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
	&rfic_lpf_bw_attribute.attr,
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



// r12: does any registered channel carry the RF_ON_TDD enable? (the mode-1b
// discriminator for the positional-TX viability oracle - see la9310_rfnm.c)
static int rfnm_dgb_tdd_ch_present(void) {
	int d, c;
	for(d = 0; d < 2; d++) {
		if(!rfnm_dgb[d]) {
			continue;
		}
		for(c = 0; c < rfnm_dgb[d]->rx_ch_cnt; c++) {
			if(rfnm_dgb[d]->rx_ch[c]->enable == RFNM_CH_RF_ON_TDD) {
				return 1;
			}
		}
		for(c = 0; c < rfnm_dgb[d]->tx_ch_cnt; c++) {
			if(rfnm_dgb[d]->tx_ch[c]->enable == RFNM_CH_RF_ON_TDD) {
				return 1;
			}
		}
	}
	return 0;
}

void rfnm_dgb_reset_sm(void) {

    // Reset ADC/DAC streaming states
    memset(&rfnm_rx_adc_s[0], 0, 4);
    rfnm_tx_dac_s = 0;

    // Reset the work result structure that stores cc_rx/cc_tx
    memset(&rfnm_dev_work_res, 0, sizeof(struct rfnm_dev_get_set_result));

    // P3/D6: stale channel enables die at the ownership boundary. A killed client
    // left its channels enabled and the NEXT client's apply validation tripped on
    // them - which pushed the rx_disable_stale_channels() ritual into every
    // consumer. The reset owns the flags now (physical FE state of unused channels
    // is unchanged by this, exactly as it was when the ritual was skipped).
    // agc dies here too - it designates an autonomous kernel servo, and one
    // surviving the boundary keeps stepping gain under the next owner's manual-gain
    // session (the "rmmod rfnm_agc before TDD" ritual was this defect's shadow).
    // Same line as enables: session policy resets, physical FE state persists.
    {
        int d, c;
        for(d = 0; d < 2; d++) {
            if(!rfnm_dgb[d]) {
                continue;
            }
            for(c = 0; c < rfnm_dgb[d]->rx_ch_cnt; c++) {
                rfnm_dgb[d]->rx_ch[c]->enable = RFNM_CH_RF_OFF;
                rfnm_dgb[d]->rx_ch[c]->stream = RFNM_CH_STREAM_OFF;
                rfnm_dgb[d]->rx_ch[c]->agc = RFNM_AGC_OFF;
            }
            for(c = 0; c < rfnm_dgb[d]->tx_ch_cnt; c++) {
                rfnm_dgb[d]->tx_ch[c]->enable = RFNM_CH_RF_OFF;
                rfnm_dgb[d]->tx_ch[c]->stream = RFNM_CH_STREAM_OFF;
            }
            // The latch shadow only tracks the KERNEL's own writes - under a
            // TDD pattern the M7 flips the physical latches without telling us, so a
            // shadow that survives the session boundary makes the next apply skip
            // re-drives it wrongly believes current (plain TX after any pattern
            // session aired dark until reboot). 0xFF = the probe-time sentinel; no
            // legal latch value matches it, so the first apply re-drives every latch.
            memset(&rfnm_dgb[d]->fe.latch_val_last_written, 0xff,
                    sizeof(rfnm_dgb[d]->fe.latch_val_last_written));
        }
    }

    // Queued chlist applies are NOT cancelled here: a client that requested this
    // reset may already have enqueued its first applies (the reset runs async), and
    // dropping them silently orphans the client's confirmed-apply protocol. The works
    // self-serialize via rfnm_wait_restart_sm_idle, which now outlasts a hard reset.

    // Force a stream update with current (reset) state
    //rfnm_la9310_stream(rfnm_user_samp_rate_hz, rfnm_tx_dac_s, rfnm_rx_adc_s);

    printk("RFNM: Daughterboard state machine reset complete\n");
}

static __init int rfnm_daughterboard_init(void)
{
	extern int (*rfnm_tdd_ch_present_cb)(void);
	printk("init rfnm_daughterboard\n");
	rfnm_tdd_ch_present_cb = rfnm_dgb_tdd_ch_present;	// r12 mode-1b oracle

	rfnm_tx_idle_park_cb = rfnm_dgb_tx_idle_park;	// park armed TX synths at radio-idle

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

	si5510_i2c_dev = bus_find_device_by_name(&i2c_bus_type, NULL, "0-0058");
	if (!si5510_i2c_dev) {
		printk("Couldn't find i2c device\n");
	} else {
		si5510_i2c_client = i2c_verify_client(si5510_i2c_dev);
		if (!si5510_i2c_client) {
			printk("Couldn't find i2c client\n");
		}
	}

	rfnm_user_samp_rate_hz = 122880000;
		
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

	rfnm_register_reset_dgb_cb(rfnm_dgb_reset_sm);

	return 0;
}

static __exit void rfnm_daughterboard_exit(void)
{
	//kobject_put(&foo->kobj);
	//kset_unregister(rfnm_dgb_primary_kset);

	rfnm_tx_idle_park_cb = NULL;
	// tdd_ch_present was registered at init but never retracted - same
	// freed-text class as the lalib ptmr_now oops; null + settle before unload
	{
		extern int (*rfnm_tdd_ch_present_cb)(void);
		rfnm_tdd_ch_present_cb = NULL;
	}
	cancel_work_sync(&rfnm_dgb_tx_idle_park_w);
	msleep(20);

	put_device(si5510_i2c_dev);

	memunmap(bootcfg);
}


MODULE_PARM_DESC(device, "RFNM Granita Daughterboard Driver");
module_init(rfnm_daughterboard_init);
module_exit(rfnm_daughterboard_exit);
MODULE_LICENSE("GPL");
