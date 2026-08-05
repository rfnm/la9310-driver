// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 RFNM

#ifndef __RFNM_STATUS_EXT_H__
#define __RFNM_STATUS_EXT_H__

// Served in hwinfo.protocol_version INSTEAD of the kernel-tree RFNM_PROTOCOL_VERSION
// (bumping the tree header regenerates kernel.release = the vermagic trap; the module
// owns the wire surface, so it owns the version). librfnm's rfnm_fw_api.h must match
// BOTH this number and the struct tail below. Standalone-includable with
// RFNM_STATUS_EXT_NO_STRUCT for units that only need the version constant.
#define RFNM_PROTOCOL_VERSION_EXT 5

// v4: which channel-config field a validation rejection was about. Rides
// rfnm_dev_get_set_result_ext next to the existing per-channel ecodes; librfnm
// composes the human message from (field, ecode) + the requested value it already
// holds + the advertised ranges from the channel list. Transient device conditions
// are NEVER represented here - the kernel owns those (they are not client errors).
enum rfnm_reject_field {
	RFNM_REJ_NONE = 0,
	RFNM_REJ_FREQ,
	RFNM_REJ_LPF_BW,
	RFNM_REJ_POWER,		// tx power
	RFNM_REJ_GAIN,		// rx gain
	RFNM_REJ_DC_TRIM,	// rx rfic_dc_i/q
	RFNM_REJ_ENABLE,
	RFNM_REJ_STREAM,
	RFNM_REJ_AGC,
	RFNM_REJ_BIAS_TEE,
	RFNM_REJ_FM_NOTCH,
	RFNM_REJ_PATH,
	RFNM_REJ_RATE,		// samp-rate / stream-planner stage
	RFNM_REJ_CH_MISSING,	// apply bit named a channel this board doesn't have
	RFNM_REJ_DEVICE,	// device-layer set failed (not a validation field)
	RFNM_REJ_BOOT,		// bring-up incomplete, session verb refused (bring-up gate)
};

#endif	/* __RFNM_STATUS_EXT_H__ */

// The struct half sits under its OWN guard so a unit that first included the header
// version-only (RFNM_STATUS_EXT_NO_STRUCT, before the kernel-tree API types are in
// scope) can include it AGAIN after <linux/rfnm-shared.h>/<linux/rfnm-api.h> to pick
// up the wire structs (rfnm_daughterboard.c does exactly this).
#ifndef RFNM_STATUS_EXT_NO_STRUCT
#ifndef __RFNM_STATUS_EXT_STRUCTS__
#define __RFNM_STATUS_EXT_STRUCTS__
// dev_status wire extension (exactness plan 2026-07-10): appended past the kernel-tree
// struct so the fleet Image stays untouched; librfnm's rfnm_fw_api.h mirrors this
// layout - keep the two in sync. Every GET_DEV_STATUS path serves the extended struct.
// Include AFTER <linux/rfnm-api.h> is in scope - that header is not self-contained
// (its packed-struct macro comes from the including unit).
struct rfnm_dev_status_ext {
	struct rfnm_dev_status base;
	uint32_t tx_feed_lead_ticks;	// TX pump feed lead the kernel enforces, phytimer ticks (rate-aware)
	uint32_t rx_flush_deadline_us;	// partial RX packet flush deadline (rx_flush_us param)
	// Pump-event telemetry, cumulative since insmod: pace_rolls = mispaced-start
	// regate REQUESTS from the TX pace check; arm_repairs = self-heal re-applies that
	// carried TX (tx_t0 re-mint + M4 GO-detect repair opportunity, kernel-visible proxy)
	uint32_t tx_pace_rolls;
	uint32_t tx_arm_repairs;
	// ---- protocol v3 bundle (r6-5b, 2026-07-11) ----
	// POS placement outcomes, cumulative since insmod (diff across a session): the
	// client-side answer to "my counters read clean while the kernel dropped 92%".
	uint32_t tx_pos_placed;
	uint32_t tx_pos_late;
	uint32_t tx_pos_stale;
	uint32_t tx_pos_misaligned;
	// honest usable write-lead minimum, phytimer ticks: placement guard (16 ring
	// slots) + one publish/staleness allowance (1 ms of slots). The old
	// tx_feed_lead_ticks stays the historical advisory; THIS is the number a
	// latency-honest feeder should budget (r6 measured floor 1.0-1.2 ms at 61.44M).
	uint32_t tx_feed_min_lead_ticks;
	// the anchor congruence step anchors are minted on (384 << max(rx_dcs,tx_dcs)),
	// computed kernel-side from the APPLIED stream word - clients must consume this
	// instead of re-deriving from cached hwinfo (the r4b anchor_at_probe folklore).
	uint32_t sched_anchor_step_ticks;
	// RESERVED 0 until their producers land (wire space pre-paid so the 5b
	// follow-ons don't force another version bump): the AXIQ-vs-DDR-RD underrun
	// split needs the VSPA DMEM aperture proven readable first (open item - the
	// "plain readl" claim is UNVERIFIED); slip needs the kernel slip engine.
	uint32_t tx_underrun_axiq;
	uint32_t tx_underrun_ddr_rd;
	uint32_t tx_slip_ticks;
	// ---- protocol v4 ----
	// cc of the most recent late/stale POS placement: lets a feeder attribute a
	// late event to a specific burst (last-event + counters, not a per-burst
	// ledger). Device-time-now and the RX<->TX anchor relation deliberately did
	// NOT get fields here: base.phytimer_now / base.{tx,rx}_t0 already carry the
	// inputs, the client derives (librfnm get_device_time_approx / anchor-offset
	// accessors) - no redundant wire.
	uint64_t tx_last_late_usb_cc;
	// ---- THE absolute time anchor ----
	// phytimer_now64 = (wrap_count << 32) | hw_low32, sampled FRESH at request time by
	// the kernel's single extension owner. phy_gen = LA9310 time generation (bumps at
	// commanded hard reset or a detected uncommanded restart); ticks compare only
	// within one generation. Version stays 5: additive tail, old clients simply do
	// not read past their sizeof (the v6 wave formalizes).
	uint64_t phytimer_now64;
	uint32_t phy_gen;
	uint32_t phy_rsvd;
} __attribute__((packed));	// v4: match librfnm's packed mirror exactly (the v3 tail was
				// all-u32 = identical either way; the u64 above must not
				// pick up alignment padding the mirror doesn't have)
void rfnm_populate_dev_status_ext(struct rfnm_dev_status_ext *r);

// v4: get_set_result wire extension, same pattern as dev_status_ext above (the base
// struct lives in the kernel tree - vermagic trap - so the module appends). Every
// GET_SET_RESULT path serves this; the loud protocol gate at client open keeps
// short-buffer old clients from ever requesting it.
struct rfnm_dev_get_set_result_ext {
	struct rfnm_dev_get_set_result base;
	uint8_t tx_reject_field[8];	// enum rfnm_reject_field per channel, 0 = none
	uint8_t rx_reject_field[8];
	uint8_t samp_rate_reject_field;
	// ---- protocol v5: the apply timing handle ----
	// Break bits +
	// an AT-APPLY (pre-send) wire-epoch snapshot; the client settle rule is
	// "for each broken direction, dev_status epoch advanced PAST the snapshot"
	// (wrap-safe s32 diff > 0). No predicted epoch, no dynamic pending state -
	// liveness flows through the status poll the client already runs. A direction
	// that this apply turned OFF gets no bit: its timeline ended, nothing to wait on.
	uint8_t timing_break;		// bit0 rx break, bit1 tx break, bit2 dcs_freq reclock
	uint32_t rx_epoch_at_apply;	// wire (u32) epochs when the apply was processed,
	uint32_t tx_epoch_at_apply;	// snapshotted BEFORE the device-side work fires
} __attribute__((packed));
void rfnm_populate_dev_set_res_ext(struct rfnm_dev_get_set_result_ext *r);
#endif	/* __RFNM_STATUS_EXT_STRUCTS__ */
#endif	/* RFNM_STATUS_EXT_NO_STRUCT */
