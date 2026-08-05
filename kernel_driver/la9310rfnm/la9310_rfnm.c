// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 RFNM

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/dma-mapping.h>
#include <linux/dma-mapping.h>
#include <la9310_base.h>
#include <la9310_vspa_registry.h>
#include "la9310_rfnm.h"
#include "la9310_rfnm_callback.h"
#include "rfnm_lalib.h"
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

#include "drivers/usb/gadget/function/g_zero.h"
#include "drivers/usb/gadget/u_f.h"

#include <linux/delay.h>
#include <linux/jiffies.h>

#include <linux/debugfs.h>

#include <linux/rfnm-shared.h>
#include <linux/rfnm-vspa.h>
#include "rfnm_status_ext.h"
#include <linux/rfnm-api.h>
#include <linux/rfnm-gpio.h>

#include <linux/kthread.h>
#include <linux/list_sort.h>

#include <uapi/linux/sched.h>
#include <uapi/linux/sched/types.h>

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <la9310_host_if.h>


#define GPIO_DEBUG 0
#define RFNM_CACHE_ADDR(addr) ((unsigned long)(addr))

DECLARE_WAIT_QUEUE_HEAD(wq_out);
DECLARE_WAIT_QUEUE_HEAD(wq_in);
DECLARE_WAIT_QUEUE_HEAD(wq_usb);

//#define RFNM_ADC_BUFCNT (0x4000) // 4096 ~= 10ms
#define RFNM_ADC_BUFCNT 4096

// TX ring latency policy, in 256-sample DAC slots (16.7 us at 15.36M, 4.2 us at 61.44M):
// if a fresh write would wait longer than max_latency slots before playing, the head
// jumps to tail + min_margin. Runtime-tunable for latency experiments.
// Time-unification rework: the JIT latency-policy knobs
// died with the free-run lane; the historical 240-slot lead survives only as the
// advisory-lead basis and the pre-rate-known staleness fallback.
#define RFNM_TX_LEAD_SLOTS 240

// TX consumer idle-resync: >100 ms without a consumed packet means a (re)started or
// long-stalled stream - accept the queue head as the new cc baseline at ANY depth, so
// shallow-paced clients start from packet one (the depth hammers alone deadlocked any
// client pacing below their thresholds: nothing consumed, consumed-cc feedback frozen).
// exact TX-path event counters (readable via /sys/module/la9310rfnm/parameters/):
// cc_gap counts every consumed cc discontinuity (expect exactly 1 per stream start from
// the idle-resync; anything more = real stream holes). underrun_jump counts head
// re-placements after the DAC lapped the writer. jit_pause counts just-in-time waits
// (informational - nonzero is normal when the pool is kept full).
static ulong rfnm_tx_stat_cc_gap;
module_param(rfnm_tx_stat_cc_gap, ulong, 0644);
// underrun_jump/jit_pause stats died with their machinery; unstamped
// counts the one remaining protocol violation (no third lane - every writer stamps)
static ulong rfnm_tx_stat_unstamped;
module_param(rfnm_tx_stat_unstamped, ulong, 0644);
static ulong rfnm_tx_stat_ingested;	// packets accepted into the pool (never resets, unlike stream_stats)
module_param(rfnm_tx_stat_ingested, ulong, 0644);
static ulong rfnm_tx_stat_slow_requeue;	// ingest fell back to copy-then-requeue (spares exhausted)
module_param(rfnm_tx_stat_slow_requeue, ulong, 0644);

// REQUEUE-FIRST ingest spares: detach the completed request's full buffer, attach a
// spare, requeue in microseconds, then copy the detached buffer into the pool at
// leisure. The serialized completion->copy->requeue cycle took ~1 ms per packet and,
// with only 8 gadget requests on the ordered endpoint, drained the pipe to ~1.1k
// pkt/s; 122.88M needs 6k. Buffers are kmalloc'd (same DMA class alloc_ep_req uses -
// the memremap'd pool carveout is NOT dma_map_single-safe, so zero-copy is out).
#define RFNM_TX_INGEST_SPARES 4
static uint8_t *rfnm_tx_spare[RFNM_TX_INGEST_SPARES];
static int rfnm_tx_spare_cnt;

uint64_t rfnm_tx_slot_rate_hz;	// DAC drain rate in 256-sample slots/s; set by rfnm_lalib on stream apply
EXPORT_SYMBOL_GPL(rfnm_tx_slot_rate_hz);
// v3 bundle: the anchor congruence step for the applied stream word (384 << max dcs).
// DEFINED here (this module loads first), COMPUTED by rfnm_lalib at stream apply -
// the same ownership split as rfnm_tx_slot_rate_hz above.
uint32_t rfnm_sched_anchor_step_ticks;
EXPORT_SYMBOL_GPL(rfnm_sched_anchor_step_ticks);

static unsigned long rfnm_tx_last_consume_jiffies;
static inline int rfnm_tx_consume_idle(void) {
	// The idle threshold must exceed the longest LEGITIMATE consumption gap: the JIT
	// hysteresis drains min_margin slots between write bursts, which takes
	// min_margin/slot_rate seconds. At deep TX upsampling (u >= 7, slot rate <= 1875/s)
	// that gap passes the old fixed 100 ms, so the restart watchdog fired mid-stream,
	// scrubbed the ring to zeros, resynced, paused, scrubbed again - a loop that made
	// 128x/256x TX radiate nothing while every counter looked alive.
	unsigned int ms = 100;
	if(rfnm_tx_slot_rate_hz) {
		unsigned int drain_ms = (unsigned int)div_u64(3000ull * RFNM_TX_LEAD_SLOTS, rfnm_tx_slot_rate_hz);
		if(drain_ms > ms) {
			ms = drain_ms;
		}
	}
	return time_after(jiffies, rfnm_tx_last_consume_jiffies + msecs_to_jiffies(ms));
}

// Scrub the whole DAC ring when a TX stream (re)starts. The fw free-runs the ring, so
// stale content - including old tone bursts - replays onto the air after underruns and
// keeps a transmitted board radiating long after its stream stopped (the only cure used
// to be a reboot). ~17 MB memset + cache clean costs ~10-15 ms once per stream start.
static void rfnm_tx_scrub_ring(void);
// rfnm_tx_align_to_fw (the head-placement rule) died with the write head
// itself - the one lane places every packet by its stamp; there is no head. THE
// minimum-lead constant: the judge and the published feed contract share one source.
// The old "+1 ms staleness allowance" guarded the dead tail-estimate era - the
// judge compares against the LIVE phytimer_now64 at consume, so it protected nothing
// and set the whole scheduling floor (measured: L_min 1.07 ms by threshold while the
// real kernel transit is 15-45 us). Floor now = the 16-slot placement
// guard (fw fetch-ahead) + a margin defaulted to 48 slots so the kernel matches the
// lib's published ~64-slot scheduling-window contract (267 us at 61.44M). The margin
// is a module param so the true safe floor can be swept empirically.
static int rfnm_tx_lead_margin_slots = 48;
module_param(rfnm_tx_lead_margin_slots, int, 0644);
static inline uint32_t rfnm_tx_min_lead_ticks(uint32_t tps) {
	return (16u + (uint32_t)rfnm_tx_lead_margin_slots) * tps;
}
int rfnm_queue_local_buffer_tx(uint8_t *buf, uint32_t len, ktime_t born);
void *rfnm_claim_local_buffer_tx(uint8_t **buf);
void rfnm_commit_local_buffer_tx(void *handle);
void rfnm_abort_local_buffer_tx(void *handle);
void *rfnm_dequeue_local_buffer_rx_ref(uint8_t **buf);
void rfnm_release_local_buffer_rx_ref(void *handle);


// Stream start = clean slate: drop staged TX packets from the PREVIOUS stream (pool
// entries and completed-but-uningested gadget requests) and arm the cc/idle baseline,
// so the first fresh packet resyncs and scrubs the ring. Without this, rapid restarts
// baselined on stale packets and burned ~97 cc gaps flushing them out mid-stream.
void rfnm_tx_flush_staging(int scrub_ring);

void rfnm_pack16to12_aarch64_wrapper(uint8_t * dest, uint8_t * src, uint32_t bytes);
void rfnm_unpack12to16_aarch64_wrapper(uint8_t * dest, uint8_t * src, uint32_t bytes);
void rfnm_remap_packed_vspa_aarch64(uint8_t * dest, uint8_t * src);

// cs16 local RX fill: NEON for line-multiple quanta (full-rate path is always 256
// samples = 1024 dest bytes), C tail for deep-decimation sub-line slices. Bit layout
// matches unpack12to16.S / librfnm unpack_12_to_cs16: sample16 = 12-bit << 4.
static void rfnm_local_fill_cs16(uint8_t *dest, uint8_t *src, uint32_t elems) {
	if(((elems * 4) & 255) == 0) {
		rfnm_unpack12to16_aarch64_wrapper(dest, src, elems * 4);
	} else {
		uint32_t c;
		for(c = 0; c < elems; c++) {
			uint32_t lp = src[0] | (src[1] << 8) | ((uint32_t)src[2] << 16);
			/* emit order swapped to match the NEON store (I/Q conjugate fix) */
			((uint16_t *)dest)[0] = (uint16_t)(((lp >> 12) & 0xfff) << 4);
			((uint16_t *)dest)[1] = (uint16_t)((lp & 0xfff) << 4);
			src += 3;
			dest += 4;
		}
	}
}


static dev_t rfnm_devnum;
static struct cdev rfnm_cdev;
static struct class *rfnm_dev_class;

volatile int countdown_to_print = 0;

volatile int callback_cnt = 0;
volatile int last_callback_cnt = 0;
volatile int received_data = 0;
volatile int last_received_data = 0;
volatile int last_rcv_buf = 0;
volatile int dropped_count = 0;
volatile long long int total_processing_time = 0;
volatile long long int last_processing_time = 0;

uint8_t * tmp_usb_buffer_copy_to_be_deprecated;
uint8_t * tmp_buff_uncompress;

#define RFNM_IQFLOOD_BUFSIZE (1024*1024*2)
#define RFNM_IQFLOOD_CBSIZE (RFNM_IQFLOOD_BUFSIZE * 8)

void __iomem *gpio4_iomem;
volatile unsigned int *gpio4;
int gpio4_initial;

//uint8_t * rfnm_iqflood_vmem;
//uint8_t * rfnm_iqflood_vmem_nocache;

#define RFNM_PACKED_STRUCT( __Declaration__ ) __Declaration__ __attribute__((__packed__))



struct rfnm_bufdesc_rx *rfnm_bufdesc_rx;
struct rfnm_bufdesc_tx *rfnm_bufdesc_tx;
//volatile struct rfnm_m7_status *rfnm_m7_status;
volatile struct rfnm_la9310_status *rfnm_la9310_status;
// lalib's stream_send polls the anchors here to close the apply contract
EXPORT_SYMBOL_GPL(rfnm_la9310_status);

// v5 apply-timing handle: pre-stream wire-epoch snapshot for the daughterboard apply
// works. Zeros until the
// status page maps - a zero snapshot sits behind every live epoch, so the client
// settle test degrades to already-settled, never a hang.
void rfnm_get_wire_epochs(uint32_t *rx_e, uint32_t *tx_e) {
	*rx_e = rfnm_la9310_status ? rfnm_la9310_status->rx_epoch : 0;
	*tx_e = rfnm_la9310_status ? rfnm_la9310_status->tx_epoch : 0;
}
EXPORT_SYMBOL_GPL(rfnm_get_wire_epochs);

struct rfnm_rx_usb_cb {
	// in the buffer of rx_usb_cb outgoing usb buffers, this is the next one we are going to equeue
	// there is no tail; it's meant to overflow
	uint32_t head;
	uint32_t adc_buf[4];
	uint32_t adc_buf_size[4];
	uint32_t cc;
	uint64_t usb_cc[4];
	uint32_t usb_host_dropped;
	//uint32_t tail;
	//uint32_t reader_too_slow;
	//uint32_t writer_too_slow;
	spinlock_t writer_lock;
	spinlock_t reader_lock;
	//int read_cc;
};

struct rfnm_rx_local_cb {
	// in the buffer of rx_usb_cb outgoing usb buffers, this is the next one we are going to equeue
	// there is no tail; it's meant to overflow
	uint32_t head;
	uint32_t adc_buf[4];
	uint32_t adc_buf_size[4];
	uint32_t cc;
	uint32_t fmt[4];	// payload encoding latched at packet start (can't switch mid-fill)
	uint64_t local_cc[4];
	uint32_t local_host_dropped;
	//uint32_t tail;
	//uint32_t reader_too_slow;
	//uint32_t writer_too_slow;
	spinlock_t writer_lock;
	spinlock_t reader_lock;
	//int read_cc;
};

struct rfnm_rx_la_cb {
	//int head;
	uint32_t tail;

	uint32_t adc_cc[4];

	// phytimer phase 1 stamp-chain validation (the 1.2 hard requirement: DSP data loss
	// must never be transparent nor a fixed skewed offset). expected = previous sub's
	// stamp + size x R; a mismatch without a DISCONT/epoch resync is counted loudly in
	// la_phytimer_error and the chain re-anchors. Independent of the cc chain above -
	// the two cross-check each other.
	uint32_t expected_phytimer[4];
	uint8_t phytimer_epoch[4];
	uint8_t phytimer_valid[4];

	//int reader_too_slow;
	//int writer_too_slow;
	spinlock_t writer_lock;
	spinlock_t reader_lock;
	//int read_cc;
};

struct rfnm_rx_usb_buf *rfnm_rx_usb_buf;

struct rfnm_local_rx_pkt *rfnm_local_buf_rx;
struct rfnm_local_tx_pkt *rfnm_local_buf_tx;

// Quiet arm-death detector provider (rfnm_lalib owns the ioremap; returns the
// mask of channels with commands stuck in the DMA FIFO and nothing executing -
// the silent stuck-queue kill the fw cannot see)
uint32_t (*rfnm_arm_death_check_cb)(void);
EXPORT_SYMBOL_GPL(rfnm_arm_death_check_cb);
// DEFAULT OFF: the predicate false-positives on some legitimate gated-session state
// (>2 s sustained - NOT simple bring-up; A/B tested: one spurious regate seeds
// a park/heal storm). Enable for tuning with fire-time forensics: the fire
// must log the raw FIFO/XRUN words + session age before this defaults on.
static int rfnm_arm_death_en = 0;
module_param(rfnm_arm_death_en, int, 0644);

// RX register-autopsy provider (rfnm_lalib owns the ioremap; a direct call
// would be a reverse module dep - same callback idiom as the regate hooks)
void (*rfnm_rx18_autopsy_cb)(const char *why);
EXPORT_SYMBOL_GPL(rfnm_rx18_autopsy_cb);
// current phytimer tick provider, set by rfnm_lalib at probe (same circular-dep
// avoidance as the regate hook). Timed placement uses it to refuse past ticks,
// which would otherwise alias mod-ring and silently air a wrap late.
uint32_t (*rfnm_ptmr_now_cb)(void);
EXPORT_SYMBOL_GPL(rfnm_ptmr_now_cb);

// rfnm_lalib re-applies the live stream word after an UNCOMMANDED phytimer
// restart (the fw rebooted under a session, so the stored word no longer matches fw
// state and the same-word dedup would pin the divergence until a rate change). Never
// invoked inside a commanded hard reset - that path re-sends its own word.
void (*rfnm_phy_restart_notify_cb)(void);
EXPORT_SYMBOL_GPL(rfnm_phy_restart_notify_cb);
static atomic_t rfnm_commanded_reset_active = ATOMIC_INIT(0);

// Host<->M4 VSPA mailbox ownership handoff (RF_SW_CMD_VSPA_MBOX_HANDOFF via
// lalib's swcmd machinery - same callback inversion as the hooks above). The M4's
// ISR consumes every VSPA outbox message; during a registry kernel-swap handshake
// the host takes exclusive ownership (1) and releases after (0). Old M4 firmware
// ignores the command and the data-register fallback in startup() covers it.
int (*rfnm_vspa_handoff_cb)(int on);
EXPORT_SYMBOL_GPL(rfnm_vspa_handoff_cb);

// ---- Time unification: THE absolute time anchor ----
// One owner for extended time: (wrap_count << 32) | hw_low32 under a seqlock, wrap-
// sampled at 1 Hz (any period < 34.95 s is safe). rfnm_phytimer_now64() serves every
// kernel consumer a fresh, tear-free absolute tick; dev_status_ext publishes it per
// request. phy_gen counts LA9310 time restarts (commanded hard reset, or a backwards
// jump wrap arithmetic cannot explain = a reset nobody commanded): ticks compare only
// within one generation, and a stale-generation session's verbs return -ENODEV
// (TIME_RESET) - reopen is the recovery, never a splice.
static seqlock_t rfnm_phy64_lock;
static uint32_t rfnm_phy64_wraps;
static uint32_t rfnm_phy64_last_lo;
static int rfnm_phy64_valid;
static struct timer_list rfnm_phy64_timer;
static uint32_t rfnm_stat_phy_wraps;
module_param_named(rfnm_stat_phy_wraps, rfnm_stat_phy_wraps, uint, 0444);
static uint32_t rfnm_phy_gen = 1;
module_param_named(rfnm_stat_phy_gen, rfnm_phy_gen, uint, 0444);
static uint32_t rfnm_session_phy_gen = 1;
#define RFNM_PHY64_MAX_STEP (8u * 61440000u)	// 8 s of ticks: max believable inter-sample advance

u64 rfnm_phytimer_now64(void) {
	uint32_t lo, snap_lo, wraps;
	unsigned int seq;

	if(!rfnm_ptmr_now_cb) {
		return 0;
	}
	do {
		seq = read_seqbegin(&rfnm_phy64_lock);
		wraps = rfnm_phy64_wraps;
		snap_lo = rfnm_phy64_last_lo;
		lo = rfnm_ptmr_now_cb();
	} while(read_seqretry(&rfnm_phy64_lock, seq));
	if(rfnm_phy64_valid && lo < snap_lo) {
		wraps++;	// hw wrapped since the last 1 Hz sample; the sampler catches up
	}
	return ((u64)wraps << 32) | lo;
}
EXPORT_SYMBOL_GPL(rfnm_phytimer_now64);

// ---- Terminal faults, no repairs ----
// ONE response to every detected data-path fault: the session dies loudly and the
// client reopens (SM reset clears). No heal, no re-apply, no splice - correctness by
// construction; git history holds the deleted repair layer if anything ever gets
// deliberately re-added.
static int rfnm_session_dead;
static uint32_t rfnm_stat_session_faults;
module_param_named(rfnm_stat_session_faults, rfnm_stat_session_faults, uint, 0444);
static void rfnm_session_fault(const char *why) {
	if(READ_ONCE(rfnm_session_dead)) {
		return;
	}
	WRITE_ONCE(rfnm_session_dead, 1);
	rfnm_stat_session_faults++;
	if(rfnm_rx18_autopsy_cb) {
		rfnm_rx18_autopsy_cb(why);	// one register-truth post-mortem line, at the fault
	}
	printk("RFNM: SESSION FAULT (%s) - session dead, reopen to recover\n", why);
}

int rfnm_phy_gen_session_ok(void) {
	if(READ_ONCE(rfnm_session_dead)) {
		return 0;
	}
	if(READ_ONCE(rfnm_session_phy_gen) != READ_ONCE(rfnm_phy_gen)) {
		printk_ratelimited("RFNM: TIME_RESET - phy generation moved (session gen %u, live %u) - reopen\n",
				READ_ONCE(rfnm_session_phy_gen), READ_ONCE(rfnm_phy_gen));
		return 0;
	}
	return 1;
}
EXPORT_SYMBOL_GPL(rfnm_phy_gen_session_ok);

uint32_t rfnm_phy_gen_now(void) {
	return READ_ONCE(rfnm_phy_gen);
}
EXPORT_SYMBOL_GPL(rfnm_phy_gen_now);

static void rfnm_phy_gen_bump(const char *why) {
	write_seqlock_bh(&rfnm_phy64_lock);
	rfnm_phy64_wraps = 0;
	rfnm_phy64_valid = 0;
	write_sequnlock_bh(&rfnm_phy64_lock);
	WRITE_ONCE(rfnm_phy_gen, READ_ONCE(rfnm_phy_gen) + 1);
	printk("RFNM: phy time generation -> %u (%s) - prior tick promises are void\n", READ_ONCE(rfnm_phy_gen), why);
}

static void rfnm_phy64_timer_fn(struct timer_list *t) {
	uint32_t lo;
	int restart = 0;
	// snapshot the cross-module cb (rmmod of the registrar nulls it + settles;
	// a bare double-read here could straddle the null and call freed text)
	uint32_t (*ptmr_f)(void) = READ_ONCE(rfnm_ptmr_now_cb);
	void (*restart_f)(void) = READ_ONCE(rfnm_phy_restart_notify_cb);

	if(ptmr_f) {
		lo = ptmr_f();
		write_seqlock(&rfnm_phy64_lock);
		if(!rfnm_phy64_valid) {
			rfnm_phy64_last_lo = lo;
			rfnm_phy64_valid = 1;
		} else if(lo < rfnm_phy64_last_lo) {
			if((uint32_t)(lo - rfnm_phy64_last_lo) > RFNM_PHY64_MAX_STEP) {
				// backwards beyond any believable wrap remainder: the counter
				// restarted underneath us (a reset nobody commanded)
				restart = 1;
			} else {
				rfnm_phy64_wraps++;
				rfnm_stat_phy_wraps++;
				rfnm_phy64_last_lo = lo;
			}
		} else {
			rfnm_phy64_last_lo = lo;
		}
		write_sequnlock(&rfnm_phy64_lock);
		if(restart) {
			rfnm_phy_gen_bump("uncommanded phytimer restart detected");
			if(restart_f && !atomic_read(&rfnm_commanded_reset_active)) {
				restart_f();
			}
		}
	}
	mod_timer(&rfnm_phy64_timer, jiffies + HZ);
}

// v3 A.2: the TX_SLOT ring producer lives in rfnm_lalib (it owns the ring mapping), but
// the TX consumer here must call it. A direct call would make la9310rfnm import a symbol
// from rfnm_lalib, which already depends on us - a circular module dep that fails to load.
// Same callback-pointer break as the hooks above: rfnm_lalib assigns this to
// rfnm_la9310_txn at its init.

// filled by lalib at init (same inversion as the other cbs: lalib imports from this
// module, so a direct call the other way would be a circular module dependency)
void (*rfnm_tx_health_cb)(struct rfnm_tx_health *h);
EXPORT_SYMBOL_GPL(rfnm_tx_health_cb);
// Per-stage RX instrumentation: one stream_status read on a frozen board
// must name WHICH stage stopped (fw ADC ring vs producer vs staging gate vs request
// pool) and make the pool population arithmetic exact (births - deaths = accounted).
// Cumulative since insmod; cheap unlocked increments from the single-writer producer
// and the completion path.
static struct {
	uint64_t prod_passes;        // producer loop iterations (liveness)
	uint64_t ring_empty;         // passes slept on la_readable < 2 (fw ring dry)
	uint64_t standdown_passes;   // passes skipped in consumer stand-down
	uint32_t standdown_entries;
	uint64_t full_ok;            // full packets staged to the wire queue
	uint64_t full_gated;         // full packet dropped: stage gate (inflight/stale cap)
	uint64_t full_pool_empty;    // full packet dropped: request pool empty
	uint64_t partial_ok;         // partial packets shipped by the deadline flush
	uint64_t partial_gated;      // deadline flush skipped: stage gate
	uint64_t partial_pool_empty; // deadline flush starved: request pool empty
	uint32_t udc_pokes;          // stalled-UDC dequeue pokes
	uint32_t req_created;        // usb_ep_queue_ele births (pool population)
	uint32_t req_destroyed;      // pool deaths (ESHUTDOWN / disabled-ep completions)
	uint32_t pool_topup;         // requests re-minted at stream-IO reset (deficit refill)
	uint32_t rate_sweeps;        // dequeue-sweeps fired by the completion-rate trigger
	uint32_t queue_fail;         // IN usb_ep_queue failures (ele kfree'd AND the
	                             // usb_request LEAKED - the dwc3 "No resource"
	                             // wreckage feeder; counted, fix owed)
	uint32_t regate_sched;       // ptmr regate work schedules (overrun park heals)
	uint32_t regate_ran;         // ptmr regate work executions
	uint64_t local_deadship;     // local packets shipped while the local consumer
	                             // was dead (payload copy skipped = stale slot content
	                             // under fresh identity); must be 0 after the lane gate
} rfnm_rx18;
static uint32_t rfnm70_prints;

// consumer recency gates: the RX producer fills a consumer's buffers only while that
// consumer showed life within the last second (poll/dequeue/ioctl for local+TCP, IN
// request submission for USB). Saves a full-rate payload copy per idle consumer.
static unsigned long rfnm_local_last_consume_jiffies;

// Freshness bound for the LOCAL ioctl consumer (librfnm): a consumer slower than
// the producer can never drain the delivery queue by reading - ioctl transit tops out
// below stream rate - so it consumes an ever-stale FIFO bounded only by the pool
// depth (~991 pkts = SECONDS of lag), unflagged and unaccounted. Cull to the newest N
// packets at the GET site and DISCONT-flag the survivor: the client adopts the seam
// immediately (the freshness contract: lose samples under lag, counted, never
// silently deliver stale ones). 0 disables. rfnm_eth's TCP worker keeps legacy
// depth - socket pacing is its own flow control.
// DEFAULT 0 (opt-in): deployed STATIC-librfnm consumers wedge on the culled ccs at
// stream start (spectrumd anchored on a cc the cull removed -> "cc timeout" spin ->
// start fail 9, measured on deployed builds). Calib sessions enable via
// /sys/module/la9310rfnm/parameters/rfnm_local_rx_queue_bound; default-on only after
// the rx_stream seam-jump residual fix ships everywhere.
static int rfnm_local_rx_queue_bound = 0;
module_param(rfnm_local_rx_queue_bound, int, 0644);
static int rfnm_local_rx_queue_depth;	// under rfnm_local_buffer_in_user->list_lock
static uint64_t rfnm_local_rx_culled;
module_param(rfnm_local_rx_culled, ullong, 0444);
static void rfnm_local_rx_cull_stale(void);
static unsigned long rfnm_usb_last_submit_jiffies;
// payload encoding for local RX fill: the local chardev (librfnm on this SoC) wants
// cs16 so nobody packs/unpacks on the same CPU; the in-kernel TCP server prefers
// packed12 for wire bandwidth. Keyed to the SESSION OWNER: each transport's SM-reset
// entry stamps its preferred format (local ioctl -> CS16, eth/USB -> PACKED12), and
// the last local close below restores the PACKED12 default. It used to follow the
// chardev open COUNT, which made a merely-resident local client (the parked RTSA
// daemon) starve every remote TCP reader forever; rfnm_eth now ships
// either format, so a mismatch degrades to wire overhead, never to silence.
uint32_t rfnm_local_rx_fmt = RFNM_PACKET_FMT_PACKED12;
EXPORT_SYMBOL(rfnm_local_rx_fmt);   /* rfnm_qec gates its ring sampling on this: a local
                                     * CS16 session runs DCS/decim modes whose ring layout
                                     * violates QEC's full-rate assumptions (the
                                     * "runaway corrections" poisoning) */

// Bring-up gate: session verbs (SM reset, samp rate, channel applies) are
// REFUSED until the boot script declares the driver stack up - a client apply racing
// daughterboard init corrupted the RX substrate for the whole boot (applies then
// reported OK while no RX lane ever enabled). load_drivers/reload_some end with
//   echo 1 > /sys/module/la9310rfnm/parameters/bringup_complete
// The fallback timer below force-opens the gate (loudly) if the script dies mid-way,
// so a broken boot stays remotely commandable instead of refusing forever.
int rfnm_bringup_complete;
module_param(rfnm_bringup_complete, int, 0644);
MODULE_PARM_DESC(rfnm_bringup_complete, "0 = bring-up in progress, session verbs refused; set to 1 by the boot script");
EXPORT_SYMBOL(rfnm_bringup_complete);
static void rfnm_bringup_timeout_fn(struct work_struct *work)
{
	if (!READ_ONCE(rfnm_bringup_complete)) {
		pr_warn("RFNM: bring-up gate released by 60 s timeout - the boot script never signaled bringup_complete; investigate load_drivers\n");
		WRITE_ONCE(rfnm_bringup_complete, 1);
	}
}
static DECLARE_DELAYED_WORK(rfnm_bringup_timeout_work, rfnm_bringup_timeout_fn);
// bumped by rfnm_agc on every gain step; rfnm_qec restarts its settle window and drops the
// in-flight accumulation on a change (LNA steps move the IQ imbalance mid-estimate)
atomic_t rfnm_rx_gain_epoch = ATOMIC_INIT(0);
EXPORT_SYMBOL(rfnm_rx_gain_epoch);
// bumped by the dgb driver at the end of every successful RX apply (retune, gain, trim -
// anything that moves the analog state); rfnm_qec re-arms its one-shot DCOFF null and
// restarts its settle window on a change
atomic_t rfnm_rx_apply_epoch = ATOMIC_INIT(0);
EXPORT_SYMBOL(rfnm_rx_apply_epoch);
static atomic_t rfnm_data_ep_openers = ATOMIC_INIT(0);
// zero-copy pool slots checked out to userspace via RFNM_LOCAL_RX_GET/TX_ACQ, indexed
// by slot; xchg() makes put/submit race-proof against double calls and fd teardown
static void *rfnm_local_zc_rx_owned[RFNM_IQFLOOD_LOCAL_RX_BUF_SIZE];
static void *rfnm_local_zc_tx_owned[RFNM_IQFLOOD_LOCAL_TX_BUF_SIZE];

// Stream worker liveness: set by start_sm_work, decremented by each worker on exit.
// stop_sm waits on this instead of the per-flag handshakes: a stop flag can stay set
// forever if the worker is already gone, which made timeout-then-retry wedge on the
// flag while the system was actually quiescent.
static atomic_t rfnm_sm_workers_alive = ATOMIC_INIT(0);


struct rfnm_tx_la_cb {
	//int head;
	uint32_t head;
	
	uint32_t dac_cc;
	uint64_t usb_cc;
	
	//int reader_too_slow;
	//int writer_too_slow;
	spinlock_t writer_lock;
	spinlock_t reader_lock;
	//int read_cc;
};

struct rfnm_usb_req_buffer {
	spinlock_t list_lock;
	struct list_head active;
};

struct rfnm_local_buffer {
	spinlock_t list_lock;
	struct list_head active;
};

struct rfnm_local_buffer_queue_ele {
	struct list_head head;
	uint64_t local_cc;
	uint32_t addr;
	uint32_t owner;
	// r6 transit-lag meter: when the packet's bytes reached board memory (gadget
	// completion / TCP claim) and when it entered the consumer's pending list.
	ktime_t born_kt;
	ktime_t enq_kt;
};



struct rfnm_stream_stats rfnm_stream_stats;

// Permanent witnesses for the closed consumer-theft defect (regression tripwires):
// open_refused counts refused second-consumer processes (the guard's counter),
// the canary counts GET-stream slot-ring skips (any future consumer-theft or
// order regression fires it). Everything else from the hunt is de-instrumented.
static uint64_t rfnm67_open_refused;
static uint64_t rfnm67_canary;
static uint32_t rfnm67_last_addr = 0xFFFFFFFF;



struct rfnm_usb_req_buffer *rfnm_usb_req_buffer_in;
struct rfnm_usb_req_buffer *rfnm_usb_req_buffer_in_usb;
struct rfnm_usb_req_buffer *rfnm_usb_req_buffer_out;
struct rfnm_usb_req_buffer *rfnm_usb_req_buffer_out_usb;

struct rfnm_local_buffer *rfnm_local_buffer_in;
// in-flight RX usb requests: bounded depth + flushable at stream start (low-rate reliability)
struct rfnm_usb_req_buffer *rfnm_usb_req_inflight_in;
static atomic_t rfnm_usb_inflight_cnt = ATOMIC_INIT(0);
static int rfnm_usb_inflight_gate_armed;
static uint32_t rfnm_usb_in_starved_periods;	// consecutive starved detector periods -> sweep at 3

// registered by rfnm_usb_function.ko (which depends on this module, so a direct
// symbol import here would be circular and break load order)
static int (*rfnm_usb_rearm_cb)(void);
void rfnm_register_usb_rearm(int (*fn)(void)) {
	rfnm_usb_rearm_cb = fn;
}
EXPORT_SYMBOL_GPL(rfnm_register_usb_rearm);
// deep enough that the stager keeps calling usb_ep_queue through a dwc3 missed-kick
// (a new queue on the endpoint is what re-kicks its pending transfers; capping at 16
// stopped the queueing exactly when the ring stalled - the self-inflicted USB wedge)
#define RFNM_USB_INFLIGHT_CAP 64

// The IN endpoints, learned as elements enroll through the completion
// path. ESHUTDOWN-class deaths shrink the pool and nothing refills it (fresh
// allocations happen only at host-driven set_alt, which USB session opens never
// perform) - the stream-IO reset re-mints the deficit against these eps, no ep state
// touched (device-side rearm stays absent by design: SS seq-state desync).
#define RFNM_USB_IN_EPS_MAX 8
static struct usb_ep *rfnm_usb_in_eps[RFNM_USB_IN_EPS_MAX];
static int rfnm_usb_in_ep_cnt;
static DEFINE_SPINLOCK(rfnm_usb_in_ep_lock);
// Status-0 IN completions, for the rate-based sweep trigger (the
// starved-streak escalation is defeated by trickle flicker; the deadline flush
// guarantees a healthy session hundreds of completions/s at ANY sample rate, so a
// collapsed completion rate with a live consumer and inflight work IS the wedge)
static atomic_t rfnm_usb_in_done_cnt = ATOMIC_INIT(0);

// The IN stager repoints req->buf into the memremap'd USB RX carveout (rfnm_rx_usb_buf);
// such a pointer must NEVER reach kfree()/free_ep_req() - kfree of a carveout interior
// pointer corrupts the allocator. Every teardown path that used free_ep_req on a
// repointed request (endpoint disable with armed requests: set_alt, bus reset, and now
// the per-session rearm) was a latent heap-corruption source.
static inline int rfnm_usb_buf_in_rx_carveout(const void *buf) {
	const uint8_t *b = buf;
	const uint8_t *base = (const uint8_t *) rfnm_rx_usb_buf;

	return rfnm_rx_usb_buf && b >= base && b < base + sizeof(struct rfnm_rx_usb_buf) * RFNM_RX_USB_BUF_SIZE;
}

// free a gadget request wherever its buffer currently points: an original kmalloc'd
// buffer is released, a carveout-repointed buffer is left alone (the ring owns it)
static void rfnm_usb_req_free(struct usb_ep *ep, struct usb_request *req) {
	if(req->buf && !rfnm_usb_buf_in_rx_carveout(req->buf)) {
		kfree(req->buf);
	}
	req->buf = NULL;
	usb_ep_free_request(ep, req);
}


// repoint an IN request at a ring slot for shipping; the first repoint after alloc
// releases the request's original kmalloc'd buffer (it used to just leak here, one
// buffer per request per endpoint (re)enable)
static inline void rfnm_usb_req_point_at_ring(struct usb_request *req, void *ring_slot) {
	if(req->buf && !rfnm_usb_buf_in_rx_carveout(req->buf)) {
		kfree(req->buf);
	}
	req->buf = ring_slot;
}

// Consumer recency, honest version: a USB RX consumer is alive only while SUCCESSFUL IN
// completions are flowing. Error-status givebacks (host cancel, session drains, endpoint
// teardown, and the dwc3 lost-event stall-work reclaim) must NOT count as life. They used
// to: the dwc3 watchdog's periodic reclaim of a dead session's armed requests refreshed
// the recency stamp, the producer kept filling, the stager kept re-queueing, and the
// watchdog churned that armed state between sessions forever - the standing churn that
// eventually raced the next session's bring-up into the SoC wedge.
static inline int rfnm_usb_rx_consumer_alive(void) {
	unsigned long last = READ_ONCE(rfnm_usb_last_submit_jiffies);

	return last && time_before(jiffies, last + HZ);
}
// A stale consumer is not a hard staging stop but a depth limit: a sparse schedule can
// legitimately idle the IN direction past the recency window, and a hard gate would then
// starve its next window forever (staging needs completions, completions need staging).
// A stale stager may keep up to this many requests armed - enough that the next host
// read revives the pipeline in one round trip, small enough that a dead session parks
// only a handful of requests for the dwc3 watchdog to (once) stand down on.
#define RFNM_USB_INFLIGHT_STALE_CAP 4
// New-session head grace: a session that follows another session's staleness must not
// inherit its depth limit. The recency stamp can only go fresh on a status-0 IN
// completion, which needs the NEW client's first reads - and librfnm arms its read URBs
// only after apply() returns and the app's buffer pool is built, tens of ms after the
// producer starts. At 61.44M that ramp is ~50-70 packets; with the stale cap at 4,
// everything past the 4 armed requests was shed (DISCONT-flagged) - the measured
// windowed->continuous head seam (~50 packets), because a gated session's last
// completion is >1 s old at the next stream start while a continuous one's is not.
// From the stream-IO reset (the "new client incoming" barrier) until the first status-0
// IN completion, the stager runs at full depth and the payload copy is not elided. The
// grace is one-shot per reset: it ends PERMANENTLY at the first status-0 completion, so
// a consumer that goes stale MID-stream still clamps to the stale cap and a dead
// session's parked state stays bounded; the deadline bounds a client that resets, arms
// a stream and dies before reading (worst case: one full-depth arming of the 64-request
// pool plus the payload copies for RFNM_USB_HEAD_GRACE_MS, once, then the stale cap).
// The UDC-stall poke deliberately does NOT honor grace: before the first completion
// there is nothing wedged to poke, and an ENDXFER racing the client's first reads makes
// dwc3 replay started requests (the duplicated-stream-head defect).
#define RFNM_USB_HEAD_GRACE_MS 3000
static unsigned long rfnm_usb_head_grace_until;	// jiffies deadline; 0 = no grace pending
static inline int rfnm_usb_rx_head_grace(void) {
	unsigned long until = READ_ONCE(rfnm_usb_head_grace_until);

	return until && time_before(jiffies, until);
}
static inline int rfnm_usb_rx_stage_gated(void) {
	int inflight = atomic_read(&rfnm_usb_inflight_cnt);

	return inflight >= RFNM_USB_INFLIGHT_CAP || (!rfnm_usb_rx_consumer_alive() && !rfnm_usb_rx_head_grace() && inflight >= RFNM_USB_INFLIGHT_STALE_CAP);
}

// same recency contract for the local side (chardev + in-kernel TCP): every dequeue
// ATTEMPT stamps rfnm_local_last_consume_jiffies (poll, ioctl, rx_available), so an
// attached-but-momentarily-empty consumer stays alive and can never bootstrap-deadlock
static inline int rfnm_local_rx_consumer_alive(void) {
	return time_before(jiffies, rfnm_local_last_consume_jiffies + HZ);
}

// Host CONTROL-plane liveness, for the producer stand-down below: ep0 vendor verbs
// (librfnm API traffic, schedule pushes, stream commands) and set_alt prove a live
// client even while the IN data direction is legitimately idle (sparse schedules park
// completions far past the recency window - the exact reason the stager's stale cap is
// a depth limit and not a hard gate). Stamped from rfnm_usb_function's setup/set_alt
// handlers and from the stream io reset, so every session start revives the producer
// before its first window's data can arrive (~40 ms schedule pre-feed contract).
static unsigned long rfnm_host_last_ctrl_jiffies;
void rfnm_note_host_ctrl(void) {
	WRITE_ONCE(rfnm_host_last_ctrl_jiffies, jiffies ? jiffies : 1);
}
EXPORT_SYMBOL_GPL(rfnm_note_host_ctrl);
static inline int rfnm_host_ctrl_alive(void) {
	unsigned long last = READ_ONCE(rfnm_host_last_ctrl_jiffies);

	return last && time_before(jiffies, last + HZ);
}

// runtime kill switch for the producer stand-down (diagnosis aid: 0 restores the old
// always-processing producer, e.g. to reproduce the dead-session LA-ring runaway)
static int rfnm_rx_standdown_en = 1;
module_param(rfnm_rx_standdown_en, int, 0644);
MODULE_PARM_DESC(rfnm_rx_standdown_en, "idle the RX producer when no consumer on any transport shows life (default 1)");

// variable-size packets: flush a partial rx usb packet after this age so latency is bounded by
// the deadline instead of the packet fill time (20480 samples = 2-60 ms at low rates)
static int rx_flush_us = 500;
module_param(rx_flush_us, int, 0644);
MODULE_PARM_DESC(rx_flush_us, "flush partial RX usb packets after this many us (0 = only full packets)");
// r6 RX latency lever: ship USB RX packets once they hold this many 256-sample slots
// instead of the full 80 (0 = full packets only). The 80-slot fill alone is 333 us at
// 61.44M - the dominant term of RX air->host latency, which comes straight out of a
// closed-loop client's TX write lead (the NR-UE budget). Rides the r5 boundary-age
// mechanism, so the wire sees the established variable-size partial packets and the
// ship path/accounting are unchanged. Needs rx_flush_us > 0 (the tail flush IS the
// ship path). 40 slots = 167 us fill / 6k pkt/s at 61.44M (the proven packet rate);
// 20 = 83 us / 12k pkt/s (validate before trusting).
static int rx_ship_slots;
module_param(rx_ship_slots, int, 0644);
MODULE_PARM_DESC(rx_ship_slots, "early-ship USB RX packets at this many slots (0 = full 80-slot packets)");
static ktime_t rfnm_rx_pkt_birth[4];
static int rfnm_prod_nap_pending;	// r5 partials: idle paths defer their nap past the tail flush
static uint64_t rfnm_rx_partial_flush_cnt;

// deep-mode diagnosis: ground-truth worker throughput counters
static uint64_t rfnm_dbg_slots, rfnm_dbg_subs, rfnm_dbg_break0, rfnm_dbg_passes;
struct rfnm_local_buffer *rfnm_local_buffer_in_user;
struct rfnm_local_buffer *rfnm_local_buffer_out;
struct rfnm_local_buffer *rfnm_local_buffer_out_user;

wait_queue_head_t local_rx_poll;
EXPORT_SYMBOL(local_rx_poll);
wait_queue_head_t local_tx_poll;
EXPORT_SYMBOL(local_tx_poll);

enum {
	RFNM_USB_EP_OK,
	RFNM_USB_EP_DEAD,
	RFNM_USB_EP_OVERFLOW,
	RFNM_USB_EP_DEFAULT,
	RFNM_USB_EP_REMOTEIO,	
	RFNM_USB_EP_MAX,	
};

int rfnm_ep_stats[RFNM_USB_EP_MAX];	

struct usb_ep_queue_ele {
	struct usb_ep *ep;
	struct usb_request *req;
	struct list_head head;
	uint64_t usb_cc;
	ktime_t born_kt;	// r6 transit-lag meter: gadget completion time (OUT direction)
	// rx seam instrumentation: header snapshot at SHIP time; the completion tripwire
	// compares it against the staging slot the request actually transmitted from
	uint64_t seam_cc;
	uint32_t seam_pt;
};

struct rfnm_dev {
	struct rfnm_rx_usb_cb rx_usb_cb;
	struct rfnm_rx_local_cb rx_local_cb;
	
	struct rfnm_rx_la_cb rx_la_cb;
	struct rfnm_tx_la_cb tx_la_cb;
	uint8_t * usb_config_buffer;
	
	int usb_flushmode;

	int wq_stop_in;
	int wq_stop_out;
	int wq_stop_usb;

	
};

#define CONFIG_DESCRIPTOR_MAX_SIZE 1000

struct rfnm_dev *rfnm_dev;


#define DRIVER_VENDOR_ID	0x0525 /* NetChip */
#define DRIVER_PRODUCT_ID	0xc0de /* undefined */

#define USB_DEBUG_MAX_PACKET_SIZE     8
#define DBGP_REQ_EP0_LEN              128
#define DBGP_REQ_LEN                  512


void rfnm_pack16to12_aarch64(uint8_t * dest, uint8_t * src, uint32_t bytes);
void rfnm_unpack12to16_aarch64(uint8_t * dest, uint8_t * src, uint32_t bytes);

/*
static inline size_t list_count_nodes(struct list_head *head)
{
	struct list_head *pos;
	size_t count = 0;

	list_for_each(pos, head)
		count++;

	return count;
}
*/
struct __attribute__((__packed__)) rfnm_packet_head {
		uint32_t check;
	uint32_t cc;
	uint8_t reader_too_slow;
	uint8_t padding[16 - 9 + 4 + 12];
};

#include <linux/ratelimit.h>
#include <linux/jiffies.h>
#include <linux/atomic.h>

#define UNFLOOD_MAX 8


static struct ratelimit_state unflood_rs[UNFLOOD_MAX];
uint32_t              unflood_cnt[UNFLOOD_MAX] = {0};

static inline void pr_unflood(uint32_t id, const char *fmt, ...)
{
    va_list args;

    if (likely(__ratelimit(&unflood_rs[id]))) {               
    
        if (unflood_cnt[id])
            //printk(KERN_WARNING "%u messages suppressed\n", unflood_cnt[id]);
			unflood_cnt[id] = 0;

        va_start(args, fmt);
        vprintk(fmt, args);
        va_end(args);
    } else {
        unflood_cnt[id]++;
    }
}





#define RFNM_PACKET_HEAD_SIZE sizeof (struct rfnm_packet_head)

// Halting an endpoint that ESHUTDOWN/ENODEV already tore down oopses in dwc3:
// usb_ep_disable NULLs ep->desc and __dwc3_gadget_ep_set_halt dereferences
// desc->bmAttributes (the 0x3 NULL-offset abort, duplex teardown storm). Halt
// only live endpoints that failed for a reason a halt can express.
static void rfnm_usb_ep_halt_guarded(struct usb_ep *ep, int status) {
	if(status == -ESHUTDOWN || status == -ENODEV || status == -ECONNRESET) {
		return;
	}
	if(!ep->enabled) {
		return;
	}
	usb_ep_set_halt(ep);
}

static void rfnm_usb_buffer_done_in(struct usb_ep_queue_ele *usb_ep_queue_ele)
{
	unsigned long flags;
	int status;

	spin_lock_irqsave(&rfnm_usb_req_buffer_in->list_lock, flags);
	list_del(&usb_ep_queue_ele->head);
	spin_unlock_irqrestore(&rfnm_usb_req_buffer_in->list_lock, flags);

	// track the request while queued: bounded depth, flushable at stream start, and the ele is
	// recycled through req->context by the completion handler (no kmalloc/kfree per packet)
	usb_ep_queue_ele->req->context = usb_ep_queue_ele;
	spin_lock_irqsave(&rfnm_usb_req_inflight_in->list_lock, flags);
	list_add_tail(&usb_ep_queue_ele->head, &rfnm_usb_req_inflight_in->active);
	spin_unlock_irqrestore(&rfnm_usb_req_inflight_in->list_lock, flags);
	atomic_inc(&rfnm_usb_inflight_cnt);

	status = usb_ep_queue(usb_ep_queue_ele->ep, usb_ep_queue_ele->req, GFP_ATOMIC);
	if (status) {
		spin_lock_irqsave(&rfnm_usb_req_inflight_in->list_lock, flags);
		list_del(&usb_ep_queue_ele->head);
		spin_unlock_irqrestore(&rfnm_usb_req_inflight_in->list_lock, flags);
		atomic_dec(&rfnm_usb_inflight_cnt);
		usb_ep_queue_ele->req->context = NULL;
		printk("kill %s:  resubmit %d bytes --> %d\n",usb_ep_queue_ele->ep->name, usb_ep_queue_ele->req->length, status);
		rfnm_usb_ep_halt_guarded(usb_ep_queue_ele->ep, status);
		kfree(usb_ep_queue_ele);
	}
}


// Ring protocol: the fw heartbeat-publishes its status block (~1 ms worst case), so
// tx_buf_id is read directly - the extrapolator that guessed a stale tail forward at
// the configured slot rate is gone, and with it the wedge class it bred (placement
// math running blind against a frozen estimate). Freshness is tracked via the age
// counter: if age stops advancing the fw is genuinely dead/rebooting and the ring
// math must fail loudly instead of guessing.
static uint32_t rfnm_tx_status_age_last;
static unsigned long rfnm_tx_status_age_jiffies;

// The VSPA parks (__builtin_done) whenever the session shape leaves it no ambient GO
// source: windowed TX with no window in flight, TX-only with every RX lane off. A
// parked core cannot heartbeat, so "the fw publishes ~1 ms worst case" only holds
// while something GOes it (RX chunk completions, free-run send completions, mailbox
// traffic). Freshness is therefore request/response at this boundary: a consumer that
// finds the block stale GOes the core (lalib registers the fenced CONTROL raise here)
// and re-reads on its retry cadence. A truly dead LA9310 stays stale through the GO,
// so the >1 s session-fault verdict downstream keeps its meaning.
void (*rfnm_vspa_go_cb)(void);
EXPORT_SYMBOL_GPL(rfnm_vspa_go_cb);

static int rfnm_tx_status_fresh(void) {
	uint32_t age = rfnm_la9310_status->age;

	if(age != rfnm_tx_status_age_last) {
		rfnm_tx_status_age_last = age;
		rfnm_tx_status_age_jiffies = jiffies;
		return 1;
	}
	if(time_before(jiffies, rfnm_tx_status_age_jiffies + msecs_to_jiffies(100))) {
		return 1;
	}
	if(rfnm_vspa_go_cb) {
		rfnm_vspa_go_cb();
	}
	return 0;
}

static uint32_t rfnm_tx_stat_timed_reject;
static uint32_t rfnm_tx_stat_timed_placed;
static uint32_t rfnm_tx_stat_mode_reject;
module_param_named(rfnm_tx_stat_timed_reject, rfnm_tx_stat_timed_reject, uint, 0444);
module_param_named(rfnm_tx_stat_timed_placed, rfnm_tx_stat_timed_placed, uint, 0444);
module_param_named(rfnm_tx_stat_mode_reject, rfnm_tx_stat_mode_reject, uint, 0444);

// RF hygiene for timed staging. A staged burst must never outlive its air time: the
// free-run drain replays resident DAC-ring content every ring lap (16384*256 ticks =
// 68.27 ms), which polluted the air across sessions. The retire scrub zeroes each
// placed burst's slots once its end tick has passed.
// (The retire fifo + its scrubber, the scrub-disable diagnostic knob, and the pos
// gap/EOB-ahead scrubs are deliberately DELETED. They were silence-by-memset
// compensation for two unfixed invariants - the free-run arm phase lottery and the
// always-open gate - and the retire scrub's grid-computed margin sat inside the
// measured 2.1 ms phase band, zeroing sparse timed windows PRE-AIR. Silence is the
// GATE's job (EOB machinery, successor project); teardown hygiene keeps the one
// paced ring scrub at stream stop.)

// scrub witness counters (dark-TX debugging): ratelimited prints hide scrub storms -
// these do not. Cumulative since load, one pair per zeroing site.
static ulong rfnm_tx_scrub_ring_calls;
module_param(rfnm_tx_scrub_ring_calls, ulong, 0444);

// Time unification: the TX_SLOT walker lane died (its walker was deleted; the one lane
// places TIME_VALID packets directly into the ring by absolute tick). PACKED12 timed
// payloads unpack into this staging buffer before the sub-slot offset copy.
static uint8_t rfnm_tx_time_stage[RFNM_TX_USB_BUF_MULTI * LA_TX_BASE_BUFSIZE];

// Positional free-run TX (the anchor contract). A POS_VALID packet carries the
// absolute tick of its first sample (librfnm stamps tx_t0 + feed_pos*R at qbuf time),
// so its ring slot is pure arithmetic off the fw anchor - the head stops being an
// accident of the align-to-live-tail race and "feed position 0 airs at tx_t0" holds
// exactly. Late/stale/misaligned packets are dropped loudly (a real-time contract airs
// silence, never 68 ms-late replays); gaps the client skipped are scrubbed so stale
// ring content never airs inside them. The flag rides tx_flags bit 2 with the TX
// epoch in [15:8] - keep in sync with librfnm rfnm_fw_api.h and fold into
// <linux/rfnm-api.h> at the next Image ship.
#ifndef RFNM_TX_FLAG_POS_VALID
#define RFNM_TX_FLAG_POS_VALID		(1u << 2)
#endif
static int rfnm_tx_pos_expect_valid;
static uint32_t rfnm_tx_pos_expect;	// next expected slot (gap-scrub baseline)
// P3/D3: positional-viability oracle, registered by rfnm_lalib at its init (NULL =
// viable; the cb reports whether a TDD pattern owns the drain with no schedule ring
// armed - the mode where positional TX is structurally impossible; verified)
int (*rfnm_pos_tx_viable_cb)(void);
EXPORT_SYMBOL_GPL(rfnm_pos_tx_viable_cb);
// r12: mode-1b discriminator, registered by rfnm_daughterboard at its init. A
// pattern with RF_ON_TDD channel enables is the DESIGNED TDD shape (M4 pattern
// engine owns the FE; epochs stable, drain healthy - verified) and
// positional TX is viable there; only the degenerate plain-RF_ON + pattern mode
// re-mints per window and stays refused.
int (*rfnm_tdd_ch_present_cb)(void);
EXPORT_SYMBOL_GPL(rfnm_tdd_ch_present_cb);
static uint32_t rfnm_tx_stat_pos_placed;
static uint32_t rfnm_tx_stat_pos_late;
static uint32_t rfnm_tx_stat_pos_stale;
static uint32_t rfnm_tx_stat_pos_misaligned;
static uint64_t rfnm_tx_last_late_usb_cc;	// v4: cc of the newest late/stale POS packet (burst attribution)
module_param_named(rfnm_tx_stat_pos_placed, rfnm_tx_stat_pos_placed, uint, 0444);
module_param_named(rfnm_tx_stat_pos_late, rfnm_tx_stat_pos_late, uint, 0444);
module_param_named(rfnm_tx_stat_pos_stale, rfnm_tx_stat_pos_stale, uint, 0444);
module_param_named(rfnm_tx_stat_pos_misaligned, rfnm_tx_stat_pos_misaligned, uint, 0444);
// 2 ms hard-lead (r5): placement-margin meter. gap (slots of 4.17 us at 61.44M) is the
// end-to-end pipeline margin left when the packet reaches the ring - log2 buckets +
// a running min watermark. pipeline p100 = fed_lead - gap_min*slot_time. Writable so
// a lead sweep zeroes them between runs (gap_min resets to ~0u = 4294967295).
static uint32_t rfnm_tx_pos_gap_hist[16];
static uint32_t rfnm_tx_pos_gap_min = ~0u;
module_param_array_named(rfnm_tx_pos_gap_hist, rfnm_tx_pos_gap_hist, uint, NULL, 0644);
module_param_named(rfnm_tx_pos_gap_min, rfnm_tx_pos_gap_min, uint, 0644);
// The tail-estimate zoo (extrapolate/slip/cap + publish-cadence meter)
// died - the judge compares stamps against phytimer_now64 directly; there is no tail
// estimate anywhere.
// r6 transit-lag meter: per-packet time spent (a) between gadget completion and the
// ingest thread's pool insert, (b) waiting in the consumer's pending list. log2 us
// buckets; a >5 ms total prints loudly (ratelimited) with the split, timestamped -
// the direct probe for the rare ~30 ms batch-drain class the lead sweeps surfaced.
static uint32_t rfnm_tx_lag_ingest_hist[16];
static uint32_t rfnm_tx_lag_pool_hist[16];
static uint32_t rfnm_tx_lag_max_us;
module_param_array_named(rfnm_tx_lag_ingest_hist, rfnm_tx_lag_ingest_hist, uint, NULL, 0644);
module_param_array_named(rfnm_tx_lag_pool_hist, rfnm_tx_lag_pool_hist, uint, NULL, 0644);
module_param_named(rfnm_tx_lag_max_us, rfnm_tx_lag_max_us, uint, 0644);
// r6 sparse-burst RF hygiene: the span after an EOB-flagged burst airs BEFORE the next
// burst's placement-time gap scrub reaches it whenever the feed lead is below the
// inter-burst period - the ring there still holds lap-old content (68 ms at 61.44M),
// which would radiate inside the feeder's own quiet gap (an NR UE's DL slots: self-jam).
// On EOB, scrub ahead of the burst tail immediately and advance the gap-scrub baseline
// past the scrubbed span. 240 slots = 1 ms at 61.44M; 0 disables.

// Stream-start arm-lottery detector, restored from the 5u-era pace check (deleted with
// the measured-phase route in f5c0711). The apply-time warmup mints the session's ONLY
// AXIQ request-latch arm and the mint is a known lottery (docs 5q/5r: parked-at-prime
// or flush-racing). Before the first consume of a stream episode - while the pump must
// still be free-running - measure its real pace; a bad pace triggers the regate
// self-heal (fresh stream word = fresh mint) with the packet held at the queue head,
// so a lost lottery costs start latency instead of a silent session.
// TDD-kernel bring-up forensics: 0 = NO autonomous heals at all - fw RX-lane
// parks freeze the session for register autopsy (epoch stays stable so drain
// measurements survive), the pace start-lottery check passes through (it is a
// FREE-RUN detector and structurally mis-measures a gated pump), and the
// regate worker's landed-but-reparked retry stands down. Default 1 = today.
static int rfnm_tx_pace_ok;
static uint32_t rfnm_tx_stat_pace_rolls;	// tombstone: dev-status-ext field frozen (re-rolls deleted)
module_param_named(rfnm_tx_stat_pace_rolls, rfnm_tx_stat_pace_rolls, uint, 0444);
// tombstone: dev-status-ext field frozen (the heal re-applies that incremented it are
// deleted; the counter stays declared because wire sizes are law)
uint32_t rfnm_tx_stat_arm_repairs;
EXPORT_SYMBOL_GPL(rfnm_tx_stat_arm_repairs);
module_param_named(rfnm_tx_stat_arm_repairs, rfnm_tx_stat_arm_repairs, uint, 0444);

// TX idle-park hook. The daughterboard layer registers a worker that
// RF_OFFs any TX channel a departed client left armed (parked TX synth = carrier on
// the air at the last tune = RX desense whenever the next session lands on/near it).
// Called on the RX producer's stand-down edge (idle radio, process context).
void (*rfnm_tx_idle_park_cb)(void);
EXPORT_SYMBOL_GPL(rfnm_tx_idle_park_cb);

// The start-lottery verify can only PASS or KILL - the re-roll ladder was
// a heal (git history holds it). Non-blocking two-phase
// sampler kept from step 1: arm on one pass, verdict >=2 ms of phytimer later, consume
// proceeds while measuring; epoch-flip/stale windows void the sample (a client apply
// crossing the window). A BAD verdict faults the session - reopen is the recovery.
static uint32_t rfnm_tx_pace_n1, rfnm_tx_pace_tl1, rfnm_tx_pace_e1, rfnm_tx_pace_a1;
static uint32_t rfnm_tx_pace_zero_streak;
static int rfnm_tx_pace_armed;
static unsigned long rfnm_tx_stale_since;	// stale-heartbeat fault clock; MUST clear on every fresh read

static int rfnm_tx_pace_check(void) {
	uint32_t ticks_per_slot = 128u << rfnm_la9310_status->tx_r_shift;
	uint32_t n2, tl2, e2, span, slots, exp_slots;

	if(!rfnm_tx_pace_armed) {
		rfnm_tx_pace_n1 = rfnm_ptmr_now_cb();
		rfnm_tx_pace_tl1 = rfnm_la9310_status->tx_buf_id;
		rfnm_tx_pace_e1 = rfnm_la9310_status->tx_epoch;
		rfnm_tx_pace_a1 = rfnm_la9310_status->age;
		rfnm_tx_pace_armed = 1;
		return 0;
	}
	n2 = rfnm_ptmr_now_cb();
	span = n2 - rfnm_tx_pace_n1;
	if(span < 122880) {
		return 0;	// window (2 ms of phytimer) not complete - keep consuming
	}
	tl2 = rfnm_la9310_status->tx_buf_id;
	e2 = rfnm_la9310_status->tx_epoch;
	rfnm_tx_pace_armed = 0;	// window consumed - re-arms next pass if still undecided
	slots = (tl2 - rfnm_tx_pace_tl1) % RFNM_DAC_BUFCNT;
	exp_slots = span / ticks_per_slot;

	if(span > 491520) {
		return 0;	// >8 ms between calls: wrap-unsafe sample, void it
	}
	if(rfnm_tx_pace_e1 != e2) {
		return 0;	// an epoch flip (client apply) crossed the window - void, re-arm
	}
	if(rfnm_tx_pace_a1 == rfnm_la9310_status->age) {
		// the status mirror never breathed across the window: tx_buf_id is a frozen
		// gauge, not a pump measurement - void the sample. This is the parked-core
		// shape (post-boot TX-only arm: no ambient GO source, heartbeat and pump
		// both idle until the status-freshness GO kick wakes the core ~100 ms into
		// feeding); the verdict on a dead LA9310 belongs to the >1 s stale-heartbeat
		// fault, not to a mirror misread. Measured: EVERY first TX arm after
		// a vspa (re)boot faulted here at client-feed time (~940 ms), second arms
		// never (pump completions keep a warm core awake).
		return 0;
	}
	if(slots >= exp_slots - exp_slots / 4 && slots <= exp_slots + exp_slots / 4) {
		printk("RFNM: tx pump pace OK (%u slots vs %u expected)\n", slots, exp_slots);
		rfnm_tx_pace_ok = 1;
		return 0;
	}
	if(slots > 0 && slots < exp_slots) {
		// startup ramp: the pump woke mid-window (GO-kick revival, prime latency) -
		// still measuring, not a verdict either way
		rfnm_tx_pace_zero_streak = 0;
		return 0;
	}
	if(slots == 0) {
		// a parked pump on a BREATHING mirror is only a conviction when sustained:
		// core wake -> first slot consumption is >3 ms (measured), so single
		// 2 ms zero-windows convict slow starts. 48 consecutive zero-windows
		// (~96 ms of live-mirror evidence) = the real stranded-pump failure face.
		if(++rfnm_tx_pace_zero_streak < 48) {
			return 0;
		}
	}
	printk("RFNM: tx pump pace BAD (%u slots vs %u expected in %u ticks, zero-streak %u) - start lottery lost\n",
			slots, exp_slots, span, rfnm_tx_pace_zero_streak);
	rfnm_session_fault("tx start lottery");
	rfnm_tx_pace_ok = 1;	// decided; never re-measure a dead session
	return 0;
}

// wrap-aware silence of a ring span [from, to): used by the align rule to zero the
// slots the fw will traverse between its current read position and the new head

// Debug helper: fill DAC ring slots with a CW test tone (fs/8 at
// 20000 amplitude - what the cross-board validation detector expects) for windowed-TX
// validation. Samples are u32-packed (imag[31:16] | real[15:0]).
void rfnm_tx_fill_tone(uint32_t src, uint32_t nslots) {
	static const int16_t tc[8] = { 20000, 14142, 0, -14142, -20000, -14142, 0, 14142 };
	static const int16_t ts[8] = { 0, 14142, 20000, 14142, 0, -14142, -20000, -14142 };
	uint32_t s, k;

	for(s = 0; s < nslots; s++) {
		uint32_t slot = (src + s) % RFNM_DAC_BUFCNT;
		for(k = 0; k < RFNM_LA9310_DMA_RX_SIZE; k++) {
			rfnm_bufdesc_tx[slot].buf[k] = ((uint32_t)(uint16_t)ts[k & 7] << 16) | (uint16_t)tc[k & 7];
		}
		dcache_clean_poc(RFNM_CACHE_ADDR(&rfnm_bufdesc_tx[slot]), RFNM_CACHE_ADDR(&rfnm_bufdesc_tx[slot + 1]));
	}
}
EXPORT_SYMBOL_GPL(rfnm_tx_fill_tone);

// The whole-ring variant declared at the top of the file: stream start must not air
// stale IQ from the previous session (the fw free-runs the ring, so old bursts replay
// on the air otherwise - and the gated start rebases to slot 0, right into the oldest
// stale content).
static void rfnm_tx_scrub_ring(void) {
	uint32_t s;

	rfnm_tx_scrub_ring_calls++;
	// PACED chunks, never one sweep (dark-TX class, measured): a contiguous
	// 17 MB memset+clean is a ~10 ms max-rate writeback flood that starves the
	// VSPA's inbound PCIe DDR reads (docs 5w class); a pump armed under that flood
	// wedges permanently (fetch issues advance tx_buf_id, completions never land,
	// stale zero DMEM streams at line rate - invisible to every pace check).
	// 256-slot chunks with breathing gaps keep the read path alive.
	for(s = 0; s < RFNM_DAC_BUFCNT; s += 256) {
		memset(&rfnm_bufdesc_tx[s], 0, sizeof(struct rfnm_bufdesc_tx) * 256);
		dcache_clean_poc(RFNM_CACHE_ADDR(&rfnm_bufdesc_tx[s]), RFNM_CACHE_ADDR(&rfnm_bufdesc_tx[s + 256]));
		udelay(50);
	}
}


static void rfnm_usb_buffer_done_out(struct usb_ep_queue_ele *usb_ep_queue_ele);

void rfnm_tx_flush_staging(int scrub_ring) {
	struct rfnm_local_buffer_queue_ele *ple;
	struct usb_ep_queue_ele *ue;
	int flushed = 0;

	// staged pool packets -> free list
	spin_lock(&rfnm_local_buffer_out->list_lock);
	spin_lock(&rfnm_local_buffer_out_user->list_lock);
	while((ple = list_first_entry_or_null(&rfnm_local_buffer_out->active, struct rfnm_local_buffer_queue_ele, head)) != NULL) {
		list_del(&ple->head);
		list_add_tail(&ple->head, &rfnm_local_buffer_out_user->active);
		flushed++;
	}
	spin_unlock(&rfnm_local_buffer_out_user->list_lock);
	spin_unlock(&rfnm_local_buffer_out->list_lock);

	// completed-but-uningested gadget requests -> back on the wire, content dropped
	while(1) {
		spin_lock(&rfnm_usb_req_buffer_out->list_lock);
		ue = list_first_entry_or_null(&rfnm_usb_req_buffer_out->active, struct usb_ep_queue_ele, head);
		spin_unlock(&rfnm_usb_req_buffer_out->list_lock);
		if(ue == NULL) {
			break;
		}
		rfnm_usb_buffer_done_out(ue);
		flushed++;
	}

	// silence the whole ring (RF hygiene: the fw free-runs, stale content replays
	// every lap). scrub_ring=0 = the ARM-WINDOW flavor (dark-TX class): a
	// stream-START flush must NOT write the ring at all - hygiene is already owed
	// by the stop/close/SM-reset flushes, all of which run with the pump quiesced.
	if(scrub_ring) {
		rfnm_tx_scrub_ring();
	}
	// and the next stream episode re-verifies its start-lottery arm
	rfnm_tx_pace_ok = 0;
	rfnm_tx_pace_armed = 0;
	rfnm_tx_pace_zero_streak = 0;
	rfnm_tx_stale_since = 0;
	rfnm_tx_pos_expect_valid = 0;

	// arm the idle-resync: the next consumed packet becomes the cc baseline and
	// triggers the head realign
	rfnm_tx_last_consume_jiffies = jiffies - msecs_to_jiffies(1000);
	if(flushed) {
		pr_info("RFNM: TX staging flushed at stream start (%d stale packets)\n", flushed);
	}
}
EXPORT_SYMBOL_GPL(rfnm_tx_flush_staging);


static void rfnm_usb_buffer_done_out(struct usb_ep_queue_ele *usb_ep_queue_ele)
{
	unsigned long flags;
	int status;

	spin_lock_irqsave(&rfnm_usb_req_buffer_out->list_lock, flags);
	list_del(&usb_ep_queue_ele->head);
	spin_unlock_irqrestore(&rfnm_usb_req_buffer_out->list_lock, flags);

	status = usb_ep_queue(usb_ep_queue_ele->ep, usb_ep_queue_ele->req, GFP_ATOMIC);
	if (status) {
		printk("kill %s:  resubmit %d bytes --> %d\n",usb_ep_queue_ele->ep->name, usb_ep_queue_ele->req->length, status);
		rfnm_usb_ep_halt_guarded(usb_ep_queue_ele->ep, status);
		// FIXME recover later ... somehow 
	}

	kfree(usb_ep_queue_ele);
}

#if 1
//static void rfnm_tasklet_handler_in(unsigned long tasklet_data);
//static DECLARE_TASKLET_OLD(rfnm_tasklet_in, &rfnm_tasklet_handler_in);

//static void rfnm_tasklet_handler_out(unsigned long tasklet_data);
//static DECLARE_TASKLET_OLD(rfnm_tasklet_out, &rfnm_tasklet_handler_out);
#else
void rfnm_handler_in(struct work_struct * tasklet_data);
void rfnm_handler_out(struct work_struct * tasklet_data);

struct work_struct  rfnm_tasklet_in;
struct work_struct  rfnm_tasklet_out;

DECLARE_WORK(rfnm_tasklet_in, rfnm_handler_in);
DECLARE_WORK(rfnm_tasklet_out, rfnm_handler_out);
#endif




void kernel_neon_begin(void);
void kernel_neon_end(void);

int can_run_handler_in(void) {
	struct usb_ep_queue_ele *usb_ep_queue_ele;
	struct rfnm_local_buffer_queue_ele *rfnm_local_buffer_queue_ele;

	spin_lock(&rfnm_usb_req_buffer_in->list_lock);
	usb_ep_queue_ele = list_first_entry_or_null(&rfnm_usb_req_buffer_in->active, struct usb_ep_queue_ele, head);
	spin_unlock(&rfnm_usb_req_buffer_in->list_lock);

	spin_lock(&rfnm_local_buffer_in->list_lock);
	rfnm_local_buffer_queue_ele = list_first_entry_or_null(&rfnm_local_buffer_in->active, struct rfnm_local_buffer_queue_ele, head);
	spin_unlock(&rfnm_local_buffer_in->list_lock);

	return (usb_ep_queue_ele != NULL) || (rfnm_local_buffer_queue_ele != NULL);
}

int can_run_handler_usb(void) {
	struct usb_ep_queue_ele *usb_ep_queue_ele_in;

	spin_lock(&rfnm_usb_req_buffer_in_usb->list_lock);
	usb_ep_queue_ele_in = list_first_entry_or_null(&rfnm_usb_req_buffer_in_usb->active, struct usb_ep_queue_ele, head);
	spin_unlock(&rfnm_usb_req_buffer_in_usb->list_lock);

	struct usb_ep_queue_ele *usb_ep_queue_ele_out;

	spin_lock(&rfnm_usb_req_buffer_out->list_lock);
	usb_ep_queue_ele_out = list_first_entry_or_null(&rfnm_usb_req_buffer_out->active, struct usb_ep_queue_ele, head);
	spin_unlock(&rfnm_usb_req_buffer_out->list_lock);

	
	// The head-vs-tail writable gate died with the write head. Ingest is
	// governed by the pool's own bounded backpressure; placement timing is the consume
	// loop's judge (early packets wait at the queue head, computed from the one clock).
	int out_runnable = (usb_ep_queue_ele_out != NULL);

	return ((usb_ep_queue_ele_in != NULL) || out_runnable) || rfnm_dev->usb_flushmode || rfnm_dev->wq_stop_usb;
}

//static void rfnm_handler_usb(unsigned long tasklet_data) {
//void rfnm_handler_usb(struct work_struct * tasklet_data) {
int rfnm_handler_usb(void * tasklet_data) {

	struct sched_param sparam = { .sched_priority = 1 };
sched_setscheduler(current, SCHED_FIFO, &sparam);

	while(1) {
wait:
		//usleep_range(500, 1000);
		if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_5);
		// TASK_IDLE variants here and in the pacing sleeps: these three pinned workers
		// park uninterruptibly, which made an idle board report loadavg ~5.
		// TIMEOUT: the gate can become true with NO waker -
		// on a virgin boot the first TX session's 8 OUT completions wake this thread
		// while la_writable still reads 0 (fw tail not moving yet, head==tail), and
		// when the tail starts a moment later nothing re-wakes the queue: the requests
		// never requeue and every host bulk-OUT times out until some RX session's IN
		// completions happen to wake us (the old 'RX-before-first-TX' boot ritual).
		// A 50 ms bounded re-test converts the lost wake into a <=50 ms first-TX delay.
		wait_event_idle_timeout(wq_usb, can_run_handler_usb(), HZ / 20);
		if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_5);

		//if(rfnm_dev->wq_stop_usb) {
		//	rfnm_dev->usb_flushmode = 1;
		//	printk("Stopping USB thread by entering flushmode");
		//}

		// USB OUT ingest, REQUEUE-FIRST: validate the header, swap the request's full
		// buffer for a spare and put the request back on the wire immediately, THEN copy
		// the detached buffer into the shared pool. The copy (~30 us for 61 KB) overlaps
		// with the endpoint receiving the next packet instead of serializing ahead of the
		// requeue. The local consumer (cpu2) is the single ring writer for both
		// transports; it cc-sorts its list, so arrival order is irrelevant.
		if(rfnm_tx_spare_cnt == 0 && rfnm_tx_spare[0] == NULL) {
			int sp;
			for(sp = 0; sp < RFNM_TX_INGEST_SPARES; sp++) {
				rfnm_tx_spare[sp] = kmalloc(RFNM_USB_TX_PACKET_SIZE, GFP_KERNEL);
			}
			rfnm_tx_spare_cnt = RFNM_TX_INGEST_SPARES;
		}
		while(1) {
			struct usb_ep_queue_ele *ig_ele;
			struct rfnm_tx_usb_buf *lb_in;
			uint8_t *detached = NULL;
			uint32_t need;
			ktime_t born;	// captured before done_out kfrees the element

			spin_lock(&rfnm_usb_req_buffer_out->list_lock);
			ig_ele = list_first_entry_or_null(&rfnm_usb_req_buffer_out->active, struct usb_ep_queue_ele, head);
			spin_unlock(&rfnm_usb_req_buffer_out->list_lock);
			if(ig_ele == NULL) {
				break;
			}
			lb_in = ig_ele->req->buf;
			born = ig_ele->born_kt;
			need = RFNM_USB_TX_PACKET_HEAD_SIZE + lb_in->multi * LA_TX_BASE_BUFSIZE_12;
			if(lb_in->magic != 0x758f4d4a || lb_in->multi < 1 || lb_in->multi > RFNM_TX_USB_BUF_MULTI
					|| ig_ele->req->actual != need) {
				printk_ratelimited("usb tx framing lost (magic %08x multi %u actual %u), dropping\n",
						lb_in->magic, lb_in->multi, ig_ele->req->actual);
				rfnm_stream_stats.usb_tx_error[0]++;
				rfnm_usb_buffer_done_out(ig_ele);	// recycle the request, drop the packet
				continue;
			}

			if(rfnm_tx_spare_cnt > 0) {
				detached = (uint8_t *)lb_in;
				ig_ele->req->buf = rfnm_tx_spare[--rfnm_tx_spare_cnt];
				rfnm_usb_buffer_done_out(ig_ele);	// request back on the wire in us, copy below overlaps
			}

			if(detached) {
				// the request is already flying; the pool insert should not drop data, but
				// it must NOT hold the handler hostage either: this same thread ships the
				// IN direction, so an unbounded retry here (pool congested because the DAC
				// side stalled on a transient) froze BOTH directions until the client gave
				// up - the sustained-full-duplex wedge. Bounded retry, then drop + count;
				// the client's closed-loop pacing throttles on the dropped/error counters.
				int tries = 50;
				while(rfnm_queue_local_buffer_tx(detached, need, born) && --tries) {
					usleep_idle_range(100, 200);
				}
				if(!tries) {
					rfnm_stream_stats.usb_tx_error[0]++;
					printk_ratelimited("usb tx ingest: pool congested >7 ms, dropping packet\n");
				}
				rfnm_tx_spare[rfnm_tx_spare_cnt++] = detached;
			} else {
				// spares exhausted (pool congestion): classic copy-then-requeue order,
				// which is also the natural backpressure path
				rfnm_tx_stat_slow_requeue++;
				if(rfnm_queue_local_buffer_tx((uint8_t *)lb_in, need, born)) {
					usleep_idle_range(100, 200);
					break;
				}
				rfnm_usb_buffer_done_out(ig_ele);
			}
			rfnm_stream_stats.usb_tx_ok[0]++;
			rfnm_tx_stat_ingested++;
		}

		struct usb_ep_queue_ele *usb_ep_queue_ele;
		int status;
		
		if(rfnm_dev->usb_flushmode) {
			
			struct rfnm_usb_req_buffer *flushing_queues[4];

			flushing_queues[0] = rfnm_usb_req_buffer_in;
			flushing_queues[1] = rfnm_usb_req_buffer_in_usb;
			flushing_queues[2] = rfnm_usb_req_buffer_out;
			flushing_queues[3] = rfnm_usb_req_buffer_out_usb;

			for (int q = 0; q < 4; q++) {

				while(1) {
					spin_lock(&flushing_queues[q]->list_lock);
					usb_ep_queue_ele = list_first_entry_or_null(&flushing_queues[q]->active, struct usb_ep_queue_ele, head);
					spin_unlock(&flushing_queues[q]->list_lock);

					if(usb_ep_queue_ele == NULL) {
						break;
					}

					spin_lock(&flushing_queues[q]->list_lock);
					list_del(&usb_ep_queue_ele->head);
					spin_unlock(&flushing_queues[q]->list_lock);
#if 1
					usb_ep_queue_ele->req->length = 0;
					//usb_ep_queue_ele->req->buf = rfnm_rx_usb_buf;
					//usb_ep_queue_ele->req->buf = kzalloc(0x100, GFP_KERNEL);
					status = usb_ep_queue(usb_ep_queue_ele->ep, usb_ep_queue_ele->req, GFP_ATOMIC);
					if (status) {
						printk("usb_flushmode: kill %s:  resubmit %d bytes --> %d\n",usb_ep_queue_ele->ep->name, usb_ep_queue_ele->req->length, status);
						rfnm_usb_ep_halt_guarded(usb_ep_queue_ele->ep, status);
						// FIXME recover later ... somehow 
					} else {
						printk("usb_flushmode: %s:  resubmit %d bytes --> %d (%lx)\n",usb_ep_queue_ele->ep->name, usb_ep_queue_ele->req->length, usb_ep_queue_ele->req->buf, status);
					}
#endif

					kfree(usb_ep_queue_ele);
				}
			}

			rfnm_dev->usb_flushmode = 0;

			printk("Flushmode done");
		}

		if(rfnm_dev->wq_stop_usb) {
			rfnm_dev->wq_stop_usb = 0;
			atomic_dec(&rfnm_sm_workers_alive);
			printk("Stopping USB process");
			return 0;
		}

		//printk("kill\n");
		
try_input:
		spin_lock(&rfnm_usb_req_buffer_in_usb->list_lock);
		usb_ep_queue_ele = list_first_entry_or_null(&rfnm_usb_req_buffer_in_usb->active, struct usb_ep_queue_ele, head);
		spin_unlock(&rfnm_usb_req_buffer_in_usb->list_lock);
		

		if(usb_ep_queue_ele == NULL) {
			goto try_other_direction;
		}

		spin_lock(&rfnm_usb_req_buffer_in_usb->list_lock);
		list_del(&usb_ep_queue_ele->head);
		spin_unlock(&rfnm_usb_req_buffer_in_usb->list_lock);

		dcache_clean_poc(
			RFNM_CACHE_ADDR(usb_ep_queue_ele->req->buf),
			RFNM_CACHE_ADDR((uint8_t *) usb_ep_queue_ele->req->buf + usb_ep_queue_ele->req->length));

		//printk("DQ %lx\n", usb_ep_queue_ele->req->buf);



		//memset(((uint8_t*) usb_ep_queue_ele->req->buf) + 32, 0xee, 16);

		// track the request while queued: bounded depth + flushable at stream start; the ele is
		// recycled through req->context by the completion handler (no kmalloc/kfree per packet)
		usb_ep_queue_ele->req->context = usb_ep_queue_ele;
		spin_lock(&rfnm_usb_req_inflight_in->list_lock);
		list_add_tail(&usb_ep_queue_ele->head, &rfnm_usb_req_inflight_in->active);
		spin_unlock(&rfnm_usb_req_inflight_in->list_lock);
		atomic_inc(&rfnm_usb_inflight_cnt);

		status = usb_ep_queue(usb_ep_queue_ele->ep, usb_ep_queue_ele->req, GFP_ATOMIC);
		if (status) {
			spin_lock(&rfnm_usb_req_inflight_in->list_lock);
			list_del(&usb_ep_queue_ele->head);
			spin_unlock(&rfnm_usb_req_inflight_in->list_lock);
			atomic_dec(&rfnm_usb_inflight_cnt);
			usb_ep_queue_ele->req->context = NULL;
			printk_ratelimited("kill %s:  resubmit %d bytes --> %d\n",usb_ep_queue_ele->ep->name, usb_ep_queue_ele->req->length, status);
			// WRECKAGE FIX: this path used to kfree
			// the ele and LEAK the usb_request - a SET_INTERFACE resync storm fed
			// hundreds of failures through here (short 359 measured live), eroding
			// the gadget until dwc3 "No resource for epX" killed the device until
			// power cycle. Skip the halt on teardown-class errors - halting an ep
			// that is mid-set_alt only churns the reconfiguration it is racing.
			// Pool attrition wedge: destroying the ele here is only
			// right when the ep config is going away (teardown-class status - the
			// host-driven set_alt allocates fresh ones). Device-side ep rearm at
			// session boundaries is deliberately absent (SS seq-state desync), so
			// every TRANSIENT failure destroyed here shrank the pool PERMANENTLY:
			// 44+ heal-storm failures ate it down to 16 elements, all pinned
			// inflight, staging starved, every later session born dead (0.55 Msps
			// partial trickle, both batteries). Recycle transient failures instead;
			// the stager re-points pooled requests before arming, so a stale
			// length/buf is harmless here.
			rfnm_rx18.queue_fail++;
			if(status != -ESHUTDOWN && status != -ENODEV && status != -ESRCH) {
				rfnm_usb_ep_halt_guarded(usb_ep_queue_ele->ep, status);
				spin_lock(&rfnm_usb_req_buffer_in->list_lock);
				list_add_tail(&usb_ep_queue_ele->head, &rfnm_usb_req_buffer_in->active);
				spin_unlock(&rfnm_usb_req_buffer_in->list_lock);
			} else {
				rfnm_rx18.req_destroyed++;
				rfnm_usb_req_free(usb_ep_queue_ele->ep, usb_ep_queue_ele->req);
				kfree(usb_ep_queue_ele);
			}
		}

		if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_6);
		if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_6);

try_other_direction:

		spin_lock(&rfnm_usb_req_buffer_out_usb->list_lock);
		usb_ep_queue_ele = list_first_entry_or_null(&rfnm_usb_req_buffer_out_usb->active, struct usb_ep_queue_ele, head);
		spin_unlock(&rfnm_usb_req_buffer_out_usb->list_lock);
		
		if(usb_ep_queue_ele == NULL) {
			goto wait;
		}

		//printk("%x\n", usb_ep_queue_ele);
		//printk("%x %x\n", usb_ep_queue_ele->ep, usb_ep_queue_ele->req);


		spin_lock(&rfnm_usb_req_buffer_out_usb->list_lock);
		list_del(&usb_ep_queue_ele->head);
		spin_unlock(&rfnm_usb_req_buffer_out_usb->list_lock);


		status = usb_ep_queue(usb_ep_queue_ele->ep, usb_ep_queue_ele->req, GFP_ATOMIC);
		if (status) {
			printk("kill %s:  resubmit %d bytes --> %d\n",usb_ep_queue_ele->ep->name, usb_ep_queue_ele->req->length, status);
			rfnm_usb_ep_halt_guarded(usb_ep_queue_ele->ep, status);
			// FIXME recover later ... somehow 
		}

		if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_7);
		if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_7);
		kfree(usb_ep_queue_ele);

		goto try_input;
	}
}

static struct hrtimer test_hrtimer;
struct completion setup_done;


static enum hrtimer_restart test_hrtimer_handler(struct hrtimer *timer)
{
    //pr_info("test_hrtimer_handler: %u\n", ++loop);
    hrtimer_forward_now(&test_hrtimer, ms_to_ktime(1));
	
	complete(&setup_done);
	//wake_up(&wq_in);

	if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_5);
	if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_5);

    return HRTIMER_RESTART;
}




//static void rfnm_handler_usb(unsigned long tasklet_data);
//static DECLARE_TASKLET_OLD(rfnm_tasklet_usb, &rfnm_handler_usb);
// dequeue EVERY tracked inflight request: each completes with -ECONNRESET and recycles
// through the normal completion path - the same seq-preserving mechanism the
// session-start flush uses, which is why new sessions always unwedged. Shared by the
// starved-streak escalation and the completion-rate trigger.
static void rfnm_usb_in_dequeue_sweep(const char *why) {
	static struct { struct usb_ep *ep; struct usb_request *req; } sw[RFNM_USB_INFLIGHT_CAP];
	struct usb_ep_queue_ele *le;
	int n = 0, k;

	spin_lock(&rfnm_usb_req_inflight_in->list_lock);
	list_for_each_entry(le, &rfnm_usb_req_inflight_in->active, head) {
		if(n < RFNM_USB_INFLIGHT_CAP) {
			sw[n].ep = le->ep;
			sw[n].req = le->req;
			n++;
		}
	}
	spin_unlock(&rfnm_usb_req_inflight_in->list_lock);
	pr_warn("RFNM: usb IN %s - dequeue-sweeping %d inflight\n", why, n);
	rfnm_rx18.udc_pokes++;
	for(k = 0; k < n; k++) {
		usb_ep_dequeue(sw[k].ep, sw[k].req);
	}
}

// --- rx seam instrumentation: catch the cc-contiguous stamps-early breaks
// (host classes -768/-20480 ticks). Layer discriminator: ship_chain_err = the staging
// packet ALREADY carried a wrong head stamp at ship time (pre-ship: copy/seam bug);
// inflight_overwrite = the staging slot changed under an in-flight gadget request (the
// head-sweep class the ring-advance guard comment documents). Neither firing while the
// host still breaks = lib-side. The ship log keyed by cc lets a host break (which
// prints its usb_cc) be matched against what the kernel believes it shipped for that
// cc; the last records dump into stream_status.
static struct {
	uint64_t ship_chain_err[4];
	uint64_t inflight_overwrite;
	uint64_t ship_log_wr;
} rfnm_rx_seam;
struct rfnm_rx_ship_rec { uint64_t cc; uint32_t pt; uint32_t elems; uint16_t slot; uint8_t lane; uint8_t path; };
#define RFNM_RX_SHIP_LOG_N 1024
static struct rfnm_rx_ship_rec rfnm_rx_ship_log[RFNM_RX_SHIP_LOG_N];
static uint32_t rfnm_rx_seam_expect_pt[4];
static uint8_t rfnm_rx_seam_expect_valid[4];
static uint8_t rfnm_rx_seam_epoch[4];
static uint32_t rfnm_rx_seam_print_cnt;

static void rfnm_rx_seam_ship_check(int lane, int path, struct usb_ep_queue_ele *ele)
{
	struct rfnm_rx_usb_buf *pkt = &rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[lane]];
	uint32_t pt = pkt->phytimer;
	uint32_t elems = pkt->elem_cnt;
	uint8_t epoch = (pkt->rx_flags >> 8) & 0xff;
	struct rfnm_rx_ship_rec *r = &rfnm_rx_ship_log[rfnm_rx_seam.ship_log_wr++ % RFNM_RX_SHIP_LOG_N];

	r->cc = pkt->usb_cc; r->pt = pt; r->elems = elems;
	r->slot = rfnm_dev->rx_usb_cb.adc_buf[lane]; r->lane = lane; r->path = path;
	ele->seam_cc = pkt->usb_cc;
	ele->seam_pt = pt;

	if((pkt->rx_flags & RFNM_RX_FLAG_DISCONT) || epoch != rfnm_rx_seam_epoch[lane]) {
		rfnm_rx_seam_expect_valid[lane] = 0;
		rfnm_rx_seam_epoch[lane] = epoch;
	}
	if(rfnm_rx_seam_expect_valid[lane] && pt != rfnm_rx_seam_expect_pt[lane]) {
		rfnm_rx_seam.ship_chain_err[lane]++;
		if(rfnm_rx_seam_print_cnt++ < 16) {
			pr_unflood(3, "rx SEAM ship-chain: lane %d cc %llu pt %u expected %u (delta %d) slot %u fill %u path %d\n",
				lane, (unsigned long long)pkt->usb_cc, pt, rfnm_rx_seam_expect_pt[lane],
				(int)(pt - rfnm_rx_seam_expect_pt[lane]), (unsigned)r->slot, elems, path);
		}
	}
	rfnm_rx_seam_expect_pt[lane] = pt + ((elems << rfnm_la9310_status->rx_r_shift) >> 1);
	rfnm_rx_seam_expect_valid[lane] = 1;
}

// variable-size packets: queue an aged partial RX packet to USB without waiting for it to fill.
// elem_cnt + a shortened req->length tell the host how many samples are real; USB bulk
// short-packet semantics deliver the partial transfer natively. Only called from the
// rfnm_handler_in thread, which is the sole writer of rx_usb_cb (no extra locking needed).
// The local (TCP) path keeps full-size buffers: its consumer reads fixed-size records.
static void rfnm_rx_flush_partial_usb(void) {
	int la_adc_id;

	if(rx_flush_us <= 0) {
		return;
	}

	// Rate-based sweep trigger. The starved-streak escalation is
	// defeated by trickle flicker (elements rotate slowly, the streak resets), so
	// judge the COMPLETION RATE: the deadline flush guarantees a healthy session
	// hundreds of status-0 completions/s at ANY sample rate, while the trickle-dead
	// face crawls at single digits. A live consumer + inflight work + a collapsed
	// rate for three windows = the wedge; same sweep recovery as the streak path.
	{
		static unsigned long rate_win_jiffies;
		static uint32_t rate_win_done_base;
		static int rate_slow_windows;
		uint32_t done_now = (uint32_t)atomic_read(&rfnm_usb_in_done_cnt);

		if(!rate_win_jiffies) {
			rate_win_jiffies = jiffies;
			rate_win_done_base = done_now;
		} else if(time_after(jiffies, rate_win_jiffies + msecs_to_jiffies(500))) {
			uint32_t delta = done_now - rate_win_done_base;

			if(delta < 40 && rfnm_usb_rx_consumer_alive() &&
					atomic_read(&rfnm_usb_inflight_cnt) > 0) {
				if(++rate_slow_windows >= 3) {
					char why[48];

					snprintf(why, sizeof(why), "completion rate collapsed (%u/500ms)", delta);
					rfnm_usb_in_dequeue_sweep(why);
					rfnm_rx18.rate_sweeps++;
					rate_slow_windows = 0;
				}
			} else {
				rate_slow_windows = 0;
			}
			rate_win_jiffies = jiffies;
			rate_win_done_base = done_now;
		}
	}

	for(la_adc_id = 0; la_adc_id < 4; la_adc_id++) {
		struct usb_ep_queue_ele *usb_ep_queue_ele;
		uint32_t usb_write_pos = rfnm_dev->rx_usb_cb.adc_buf_size[la_adc_id];

		if(!usb_write_pos) {
			continue;
		}

		if(ktime_us_delta(ktime_get(), rfnm_rx_pkt_birth[la_adc_id]) < rx_flush_us) {
			continue;
		}

		// The stall detector below used to be reachable ONLY at the
		// inflight cap - once pool attrition shrank the pool under the cap, the
		// detector could never arm again and the stuck inflight sat forever (the
		// terminal trickle wedge: free 0, inflight 16, udc_pokes frozen). Starved =
		// at the cap OR nothing left to stage while requests are inflight; both
		// mean completions are the only way forward and 500 ms without one (live
		// consumer) is a wedge.
		{
			int rfnm_in_starved = (atomic_read(&rfnm_usb_inflight_cnt) >= RFNM_USB_INFLIGHT_CAP);

			if(!rfnm_in_starved && atomic_read(&rfnm_usb_inflight_cnt) > 0) {
				spin_lock(&rfnm_usb_req_buffer_in->list_lock);
				rfnm_in_starved = list_empty(&rfnm_usb_req_buffer_in->active);
				spin_unlock(&rfnm_usb_req_buffer_in->list_lock);
			}
			if(!rfnm_in_starved) {
				goto rfnm_in_not_starved;
			}
		}
		{
			// self-healing: the counter and the in-flight list must agree. A completion
			// path that loses its context (or any future accounting slip) leaks the
			// counter upward and this gate then stays shut FOREVER - the whole IN
			// direction dies silently. If the gate has been closed for 500 ms, recount
			// from the list (the authoritative record) and warn about the divergence.
			static unsigned long blocked_since;

			if(!rfnm_usb_inflight_gate_armed) {
				rfnm_usb_inflight_gate_armed = 1;
				blocked_since = jiffies;
			} else if(time_after(jiffies, blocked_since + msecs_to_jiffies(500))) {
				struct usb_ep_queue_ele *le;
				int listed = 0;

				spin_lock(&rfnm_usb_req_inflight_in->list_lock);
				list_for_each_entry(le, &rfnm_usb_req_inflight_in->active, head) {
					listed++;
				}
				spin_unlock(&rfnm_usb_req_inflight_in->list_lock);
				if(listed != atomic_read(&rfnm_usb_inflight_cnt)) {
					pr_warn("RFNM: usb IN inflight counter diverged (%d counted, %d listed), repairing\n",
						atomic_read(&rfnm_usb_inflight_cnt), listed);
					atomic_set(&rfnm_usb_inflight_cnt, listed);
				} else {
					// counter and list agree and nothing completed for 500 ms: the UDC
					// stopped mapping pending requests onto TRBs (observed at dwc3
					// debugfs level: requests queued, all TRBs hwo=0, zero errors, all
					// IN endpoints at once). Dequeue the tracked requests - they complete
					// with -ECONNRESET, recycle through the normal completion path, and
					// the re-queue re-kicks the endpoint's transfer machinery. This is
					// the same mechanism the session-start flush uses, which is why new
					// sessions always unwedged. Root cause is a dwc3 missed-kick class
					// bug; this makes it a ~ms hiccup instead of a dead stream.
					// Poke the stalled endpoints: usb_ep_dequeue of the oldest inflight
					// request reaches dwc3 regardless of the (closed) staging gate and
					// triggers the lost-event reclaim machinery in the instrumented
					// dwc3. One per detector period; the RFNMDBG trail in dmesg shows
					// how far the recovery gets (ENDXFER issued/complete/kick).
					// ONLY while the consumer was recently alive: with no host reading,
					// "no completions for 500 ms" is not a wedge, it is just an absent
					// consumer - and poking parked requests every 500 ms right up to the
					// next session's ramp raced its first reads (ENDXFER vs active IN
					// transfers -> dwc3 replayed the started requests: every stream-head
					// packet arrived TWICE). Between sessions the session-start flush is
					// the recovery path; in-session the host SET_INTERFACE resync (2 s)
					// backstops the wedge if this window has expired.
					if(rfnm_usb_rx_consumer_alive()) {
						// Escalation: OAI measured the single-oldest poke
						// recovering NOTHING across whole sessions (poke every ~10 s,
						// trickle forever). Progress cannot be the reset signal - the
						// trickle face completes a few partials per second, which would
						// defer the sweep forever. Escalate on PERSISTENT STARVATION:
						// three consecutive detector periods still starved -> dequeue
						// EVERY tracked inflight; each completes with -ECONNRESET and
						// recycles through the normal completion path - the same
						// seq-preserving mechanism the session-start flush uses, which
						// is why new sessions always unwedged.
						if(++rfnm_usb_in_starved_periods >= 3) {
							rfnm_usb_in_dequeue_sweep("stall persists");
							rfnm_usb_in_starved_periods = 0;
						} else {
							struct usb_ep_queue_ele *fl;

							spin_lock(&rfnm_usb_req_inflight_in->list_lock);
							fl = list_first_entry_or_null(&rfnm_usb_req_inflight_in->active, struct usb_ep_queue_ele, head);
							spin_unlock(&rfnm_usb_req_inflight_in->list_lock);
							pr_warn_ratelimited("RFNM: usb IN pipeline stalled at the UDC, poking %s\n", fl ? fl->ep->name : "(none)");
							if(fl) {
								rfnm_rx18.udc_pokes++;
								usb_ep_dequeue(fl->ep, fl->req);
							}
						}
					}
				}
				rfnm_usb_inflight_gate_armed = 0;
			}
			return;
		}
rfnm_in_not_starved:
		rfnm_usb_in_starved_periods = 0;
		rfnm_usb_inflight_gate_armed = 0;

		if(rfnm_usb_rx_stage_gated()) {
			// stale consumer at the stale depth cap: do not stage further. Unbounded
			// staging for a consumer that stopped reading is what kept a full queue of
			// armed-but-never-completing requests on the IN endpoints between sessions
			// (see rfnm_usb_rx_consumer_alive / RFNM_USB_INFLIGHT_STALE_CAP).
			rfnm_rx18.partial_gated++;
			return;
		}

		spin_lock(&rfnm_usb_req_buffer_in->list_lock);
		usb_ep_queue_ele = list_first_entry_or_null(&rfnm_usb_req_buffer_in->active, struct usb_ep_queue_ele, head);
		if(usb_ep_queue_ele != NULL) {
			list_del(&usb_ep_queue_ele->head);
		}
		spin_unlock(&rfnm_usb_req_buffer_in->list_lock);

		if(usb_ep_queue_ele == NULL) {
			// pool empty: NOT a loss - the partial stays in its slot and this deadline
			// flush retries next pass (counting it as usb_rx_error made the counter
			// useless for telling real drops apart from full-pool backoff).
			// Live-consumer anomaly trace only (see the full-packet site: per-drop
			// printing on a dead session is a printk storm).
			if(rfnm_usb_rx_consumer_alive()) {
				pr_warn_ratelimited("RFNM: usb partial flush starved, pool empty (inflight %d)\n", atomic_read(&rfnm_usb_inflight_cnt));
			}
			rfnm_rx18.partial_pool_empty++;
			return;
		}

		// ship-time cc, same rule as the full-packet ship site
		rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].usb_cc = ++rfnm_dev->rx_usb_cb.usb_cc[la_adc_id];
		rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].elem_cnt = usb_write_pos;
		rfnm_usb_req_point_at_ring(usb_ep_queue_ele->req, (uint8_t *) &rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]]);
		usb_ep_queue_ele->req->length = RFNM_USB_RX_PACKET_HEAD_SIZE + usb_write_pos * 3;
		usb_ep_queue_ele->req->zero = 1;
		rfnm_rx_seam_ship_check(la_adc_id, 1, usb_ep_queue_ele);

		spin_lock(&rfnm_usb_req_buffer_in_usb->list_lock);
		list_add_tail(&usb_ep_queue_ele->head, &rfnm_usb_req_buffer_in_usb->active);
		spin_unlock(&rfnm_usb_req_buffer_in_usb->list_lock);

		rfnm_dev->rx_usb_cb.adc_buf_size[la_adc_id] = 0;
		rfnm_dev->rx_usb_cb.adc_buf[la_adc_id] = rfnm_dev->rx_usb_cb.head;
		if(++rfnm_dev->rx_usb_cb.head == RFNM_RX_USB_BUF_SIZE) {
			rfnm_dev->rx_usb_cb.head = 0;
		}

		rfnm_stream_stats.usb_rx_ok[0]++;
		// r5 partials: an exactly-full packet shipped here (the boundary-aged class)
		// is a FULL ship in every respect but the code path - count it honestly
		if(usb_write_pos == RFNM_RX_USB_BUF_SIZE_ELEMS) {
			rfnm_rx18.full_ok++;
		} else {
			rfnm_rx_partial_flush_cnt++;
			rfnm_rx18.partial_ok++;
		}

		wake_up(&wq_usb);
	}
}

//static void rfnm_tasklet_handler_in(unsigned long tasklet_data) {
// kernel-side catch-up drops must be visible to the client as a flagged seam (see the
// jump-forward site): one pending DISCONT mark per adc lane, per delivery path
static uint8_t rfnm_rx_jump_discont_usb;
static uint8_t rfnm_rx_jump_discont_local;
// kernel-declared seams (stream start, jump-forward, stand-down resume) re-anchor the
// cc chain SILENTLY: adopt the first observed cc per lane as the new baseline with no
// error count and no print - the seam is already DISCONT-flagged to the client, and
// counting a known seam turned one discontinuity into a per-sub mismatch storm. Mirrors
// the phytimer chain's "re-anchors on the first sub of the new stream" rule.
static uint8_t rfnm_rx_cc_anchor_pending;
// producer stand-down latch (see the stand-down block in rfnm_handler_in)
static int rfnm_rx_standdown_active;

// Bounded chain diagnostics: the cc/stamp chain fault prints share this per-stream
// budget (reset with the stream io state). pr_unflood alone still admits ~6 lines/s
// FOREVER on a persistent fault - from the SCHED_FIFO producer that printk pressure
// helped starve networking off the SoC in the dead-duplex LA-ring runaway. Stats
// (la_adc_error / la_phytimer_error) keep counting; only the prints are capped.
#define RFNM_RX_CHAIN_PRINT_BUDGET 12
static uint32_t rfnm_rx_chain_print_cnt;
static uint8_t rfnm_rx_slotdump_done;
static inline int rfnm_rx_chain_print_ok(void) {
	if(rfnm_rx_chain_print_cnt < RFNM_RX_CHAIN_PRINT_BUDGET) {
		rfnm_rx_chain_print_cnt++;
		return 1;
	}
	if(rfnm_rx_chain_print_cnt == RFNM_RX_CHAIN_PRINT_BUDGET) {
		rfnm_rx_chain_print_cnt++;
		printk("RFNM: rx chain diagnostics muted until stream reset (print budget spent, stats keep counting)\n");
	}
	return 0;
}

int rfnm_handler_in(void * tasklet_data) {
//void rfnm_handler_in(struct work_struct * tasklet_data) {


struct sched_param sparam = { .sched_priority = 1 };
sched_setscheduler(current, SCHED_FIFO, &sparam);

	

	while(1) {

		if(rfnm_dev->wq_stop_in) {
			goto exit_tasklet;
		}

		rfnm_rx18.prod_passes++;

		// r5 partials fix: the deadline flush moved to the loop TAIL (exit_tasklet).
		// At pass-top it ran BEFORE the drain, so the one packet stranded mid-fill by
		// every producer nap got shipped partial even though the subs completing it
		// were already in the LA ring in the same pass - a rigged race that made ~33%
		// of healthy-session ships partials (exact 2:1 full:partial = the once-per-
		// nap-cycle signature). Tail placement = drain first, flush what is GENUINELY
		// aged after; idle passes still flush on every nap exit via the same tail.

		//wait_event(wq_in, can_run_handler_in());


	//might_sleep();
	//__wait_event(wq_in, can_run_handler_in());	
    

	if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_1);


	

	struct usb_ep_queue_ele *usb_ep_queue_ele;
	
//tasklet_again:

	uint8_t *dcache;

	barrier();

	//uint32_t la_head = smp_load_acquire(&rfnm_m7_status->rx_head);
	//uint32_t la_head = rfnm_m7_status->rx_head;
	uint32_t la_head = rfnm_la9310_status->rx_buf_id;
	uint32_t la_tail = rfnm_dev->rx_la_cb.tail;

	// Parks are FAULTS, not heal triggers.
	// The fw parks a lane on AXIQ overrun and bumps rx_regate_cnt (zeroed at stream
	// start). With no pattern armed, any park = the session's real-time contract is
	// broken - fault immediately. Under an armed TDD pattern, seam parks arrive
	// continuously while the session flows (~70/s measured); only a lane that
	// parked and then went QUIET >1 s (rc nonzero, unchanging - nothing flows through
	// seams anymore) is a corpse. The autopsy fires once, inside the fault.
	{
		static uint32_t park_seen;
		static unsigned long park_last_change;
		uint32_t rc = rfnm_la9310_status->rx_regate_cnt;
		int pattern = rfnm_tdd_ch_present_cb && rfnm_tdd_ch_present_cb();
		if(rc != 0 && rc != park_seen) {
			park_seen = rc;
			park_last_change = jiffies;
			if(!pattern) {
				pr_unflood(4, "phytimer: fw parked an RX lane after overrun (cnt %u)\n", rc);
				rfnm_session_fault("rx lane park");
			}
		} else if(rc != 0 && pattern && time_after(jiffies, park_last_change + HZ)) {
			park_last_change = jiffies;
			rfnm_session_fault("rx lane quiet-parked under pattern");
		} else if(rc == 0) {
			park_seen = 0;
		}
	}

	// Quiet arm-death predicate (real, register-proven) - a FAULT input, never
	// a heal. Default off until proven out on the clean baseline.
	{
		static unsigned long arm_death_jiffies;
		if(rfnm_arm_death_en && rfnm_arm_death_check_cb && time_after(jiffies, arm_death_jiffies + HZ / 20)) {
			uint32_t dead = rfnm_arm_death_check_cb();
			arm_death_jiffies = jiffies;
			if(dead) {
				pr_unflood(4, "RFNM: quiet arm death (channels %04x: fifo full, nothing executing)\n", dead);
				rfnm_session_fault("quiet arm death");
			}
		}
	}

	// Producer stand-down: with no consumer on ANY transport showing life (USB status-0
	// completions, host ep0 control traffic, local/TCP dequeue attempts), full-rate
	// LA-ring processing is pure waste - and demonstrably dangerous: a SIGKILLed duplex
	// client left the fw free-running a persistently mis-chained sub sequence (3 of
	// every 6 ccs never shipped) and this SCHED_FIFO cpu1 worker spun flat out
	// validating and printing ~90k mismatching subs/s until networking starved off the
	// SoC. Skip the backlog unseen (tail = head), declare ONE seam (DISCONT to every
	// path + silent cc/stamp re-anchor on resume), and sleep. Every session-start path
	// revives this within a pass: stream io reset, ep0 verbs and set_alt stamp the ctrl
	// recency, local consumers stamp on their first dequeue attempt, and mid-session
	// USB pauses resume via their still-armed requests (or the host's 2 s SET_INTERFACE
	// backstop). The regate check above and the partial flusher at the pass top keep
	// running; the QEC sampler reads the LA ring independently and is unaffected.
	if(rfnm_rx_standdown_en && !rfnm_usb_rx_consumer_alive() && !rfnm_host_ctrl_alive() && !rfnm_local_rx_consumer_alive()) {
		rfnm_rx18.standdown_passes++;
		if(!rfnm_rx_standdown_active) {
			rfnm_rx_standdown_active = 1;
			rfnm_rx18.standdown_entries++;
			pr_info("RFNM: rx producer standing down, no live consumer on any transport\n");
			// Park any TX RF chain left armed by a departed client.
			// Session teardown is an SM reset - nothing ever sends TX RF_OFF, so the
			// TX synthesizer keeps running at the last tune forever; in TDD (UL freq
			// == DL freq) that parked carrier sits exactly on the cell and the next
			// RX session reads it as a monster DC + phase-noise floor (+10-16 dB) +
			// ~6 dB front-end compression: the "RX degrades after the board's own
			// TX cycles, reboot-only" failure face. The daughterboard layer registers the
			// worker (layering: it depends on us); fires on this stand-down edge -
			// the same "whole radio is idle" signal that gates the RX producer.
			if(rfnm_tx_idle_park_cb) {
				rfnm_tx_idle_park_cb();
			}
			rfnm_rx_jump_discont_usb = 0xf;
			rfnm_rx_jump_discont_local = 0xf;
			rfnm_rx_cc_anchor_pending = 0xf;
			for(int i = 0; i < 4; i++) {
				rfnm_dev->rx_la_cb.phytimer_valid[i] = 0;
				// drop dead partial packets: their payload predates the seam and the
				// DISCONT flag lives at packet birth - resuming an old partial would
				// bury the seam mid-packet, unflagged
				rfnm_dev->rx_usb_cb.adc_buf_size[i] = 0;
				rfnm_dev->rx_local_cb.adc_buf_size[i] = 0;
			}
		}
		rfnm_dev->rx_la_cb.tail = la_head;
		rfnm_prod_nap_pending = 1;	// r5 partials: nap at the tail, after the flush
		goto exit_tasklet;
	}
	if(rfnm_rx_standdown_active) {
		rfnm_rx_standdown_active = 0;
		pr_info("RFNM: rx producer resuming, consumer alive\n");
	}

	uint32_t la_readable = la_head - la_tail;

	if(la_head < la_tail) {
		la_readable += RFNM_ADC_BUFCNT;
	}

	if(la_readable < 2) {
		// post transport-fixes the writer guard needs only 1-2 slots; the old 32-slot gate +
		// 16-slot holdback added (32+16)*slot_period of latency and stalled for SECONDS at deep
		// decimation slot rates (15-600 slots/s)
		// trying to stay behind writer for no real reason

		rfnm_rx18.ring_empty++;
		if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_1);
		// r5 partials: nap AFTER the tail flush (exit_tasklet), never before it - a
		// pre-flush nap ages the stranded packet across the sleep and the flush then
		// ships it partial with its completing subs ALREADY in the ring (the rigged
		// race, second address). All idle paths set nap_pending; the tail naps last.
		rfnm_prod_nap_pending = 1;
		goto exit_tasklet;
	}

	la_readable -= 1;

	if(la_readable < 0) {
		la_readable = 0;
		rfnm_prod_nap_pending = 1;	// r5 partials: nap at the tail, after the flush
		goto exit_tasklet;
	}

	if(la_readable > (RFNM_ADC_BUFCNT / 4)) {
		// too many buffers behind, log error and jump forward. The skipped span is a
		// cc hole the client cannot distinguish from transit loss - mark the seam so
		// the next packet on every lane carries DISCONT and the client's reorder
		// machinery adopts the new numbering immediately (freshness contract) instead
		// of hoarding a stale queue behind reorder timeouts.
		rfnm_rx_jump_discont_usb = 0xf;
		rfnm_rx_jump_discont_local = 0xf;
		// kernel-declared seam: re-anchor the cc and stamp chains silently (the jump is
		// already counted by this print and flagged DISCONT; without the re-anchor every
		// jump also burned one spurious la_adc_error and one la_phytimer_error)
		rfnm_rx_cc_anchor_pending = 0xf;
		for(int i = 0; i < 4; i++) {
			rfnm_dev->rx_la_cb.phytimer_valid[i] = 0;
		}
		rfnm_dev->rx_la_cb.tail = la_head;
		printk("rx too many buffers behind (%d), jumping forward...\n", la_readable);
		rfnm_prod_nap_pending = 1;	// r5 partials: nap at the tail, after the flush
		goto exit_tasklet;
	}

	if(la_readable) {
	//	printk("readable %d head %d tail %d\n", la_readable, la_head, la_tail);
	}



	if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_2);
	if(la_tail + la_readable >= RFNM_ADC_BUFCNT) {
		dcache_inval_poc(RFNM_CACHE_ADDR(&rfnm_bufdesc_rx[la_tail]), RFNM_CACHE_ADDR(&rfnm_bufdesc_rx[RFNM_ADC_BUFCNT/* - 1*/]));
		dcache_inval_poc(RFNM_CACHE_ADDR(&rfnm_bufdesc_rx[0]), RFNM_CACHE_ADDR(&rfnm_bufdesc_rx[la_tail + la_readable + 1 - RFNM_ADC_BUFCNT]));
		//printk("invalid %d to %d and %d to %d\n", la_tail, RFNM_ADC_BUFCNT, 0, la_tail + la_readable + 1 - RFNM_ADC_BUFCNT);
	} else {
		dcache_inval_poc(RFNM_CACHE_ADDR(&rfnm_bufdesc_rx[la_tail]), RFNM_CACHE_ADDR(&rfnm_bufdesc_rx[la_tail + la_readable + 1]));
		//printk("invalid %d to %d \n", la_tail, la_tail + la_readable + 1);
	}
	if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_2);
	
	//dcache = (unsigned char *) &rfnm_bufdesc_rx[la_tail];
	//dcache_inval_poc(dcache, dcache + SZ_64K /*sizeof(struct rfnm_bufdesc_rx)*/);

	barrier();
	
	
 	


		rfnm_dbg_passes++;
		for(int q = 0; q < la_readable; q++) {
			rfnm_dbg_slots++;

			// re-invalidate this slot's bufdesc header lines immediately before reading:
			// the pass-top range invalidation is not sufficient against speculative refills
			// (deep modes read 12 header entries = 3 lines per slot and were seeing stale
			// garbage on 88% of slots while raw DRAM was pristine)
			dcache_inval_poc(RFNM_CACHE_ADDR(&rfnm_bufdesc_rx[la_tail].bufdesc[0]),
				RFNM_CACHE_ADDR(&rfnm_bufdesc_rx[la_tail].bufdesc[0]) + 256);

			if(rfnm_dev->wq_stop_in) {
				goto exit_tasklet;
			}

			/*uint64_t cycle_before;
    	asm("isb \n mrs %0, CNTVCT_EL0" : "=r"(cycle_before));
		barrier();
*/
		// destination buffer
		uint8_t *wd = (uint8_t *) &tmp_buff_uncompress[0];
		//source buffer
		uint8_t *ws = (uint8_t *) &rfnm_bufdesc_rx[la_tail].buf[ 0 ];
		uint32_t v = 0;
		uint32_t w = 0;
	kernel_neon_begin();
	rfnm_remap_packed_vspa_aarch64(wd, ws);
	kernel_neon_end();

	/*barrier();
		uint64_t cycle_after;
    	asm("isb \n mrs %0, CNTVCT_EL0" : "=r"(cycle_after));

		static uint64_t cycle_sum = 0;
		static uint64_t cycle_cnt = 0;
		cycle_cnt++;
		cycle_sum += (cycle_after - cycle_before);

		uint64_t cycle_avg = cycle_sum / cycle_cnt;

		if(cycle_cnt % 10000 == 0) {
			printk("Avg %lx\n", cycle_avg);
		}
	*/	

		uint32_t sub_read_size = 0;
		for(int s = 0; s < RFNM_RX_BUF_OUT_SUB_CNT; s++) {

			if(!rfnm_bufdesc_rx[la_tail].bufdesc[s].size) {
				if(s == 0) {
					rfnm_dbg_break0++;
				}
				break;
			}
			rfnm_dbg_subs++;

			//*gpio4 = *gpio4 | (0x1 << 5);
			//*gpio4 = *gpio4 & ~(0x1 << 5);

			// phytimer phase 1: the sub adc_id word is packed [7:0] id, [8] DISCONT, [23:16] epoch
			uint32_t la_adc_raw = smp_load_acquire(&rfnm_bufdesc_rx[la_tail].bufdesc[s].adc_id);
			uint32_t la_adc_id = RFNM_RX_SUBDESC_ID(la_adc_raw);
			uint32_t la_sub_discont = RFNM_RX_SUBDESC_DISCONT(la_adc_raw);
			uint32_t la_sub_epoch = RFNM_RX_SUBDESC_EPOCH(la_adc_raw);
			uint32_t la_adc_cc = rfnm_bufdesc_rx[la_tail].bufdesc[s].cc;
			uint32_t la_adc_size = rfnm_bufdesc_rx[la_tail].bufdesc[s].size;
			uint32_t la_phytimer = rfnm_bufdesc_rx[la_tail].bufdesc[s].phytimer;


			//printk("la_adc_cc %d adc_buf_cnt %d adc_buf %d head %d\n",
			//	la_adc_cc, rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id], rfnm_dev->rx_usb_cb.adc_buf[la_adc_id], rfnm_dev->rx_usb_cb.head);

			if(la_adc_id >= 4 || (la_adc_raw & ~0x00FF01FFu)) {
				pr_unflood(1, "adc buffer error, #%d tail %d size %d\n", la_adc_raw, la_tail, rfnm_bufdesc_rx[la_tail].bufdesc[s].size);
				continue;
			}

			if(la_adc_size % 64 || la_adc_size > 256) {
				// same unbounded-on-persistent-garbage class as the cc mismatch print:
				// a raw printk here ran at sub rate forever on a stuck bad header
				if(rfnm_rx_chain_print_ok()) {
					pr_unflood(1, "adc size error, %d tail %d size %d\n", la_adc_id, la_tail, la_adc_size);
				}
				continue;
			}

			// phytimer phase 1 stamp-chain validation (the 1.2 hard requirement): every sub's
			// stamp must equal the previous sub's stamp + size x R, with R = 2^rx_r_shift / 2
			// from the fw-published timing anchor. Validation engages only for subs of the
			// CURRENT epoch: rx_r_shift belongs to the current stream generation, and the
			// stale-prefix drain after a rate change otherwise validates old-epoch subs
			// against the new R and manufactures false breaks. A break within one epoch
			// without a DISCONT resync means data was lost somewhere the device did not
			// flag - count it loudly, then re-anchor so one break is one error, not a
			// storm. Cross-checks the cc chain below.
			if(la_sub_epoch != (rfnm_la9310_status->rx_epoch & 0xFF)) {
				rfnm_dev->rx_la_cb.phytimer_valid[la_adc_id] = 0;
			} else {
				if(!rfnm_dev->rx_la_cb.phytimer_valid[la_adc_id] || la_sub_epoch != rfnm_dev->rx_la_cb.phytimer_epoch[la_adc_id] || la_sub_discont) {
					rfnm_dev->rx_la_cb.phytimer_valid[la_adc_id] = 1;
					rfnm_dev->rx_la_cb.phytimer_epoch[la_adc_id] = la_sub_epoch;
				} else if(la_phytimer != rfnm_dev->rx_la_cb.expected_phytimer[la_adc_id]) {
					rfnm_stream_stats.la_phytimer_error[la_adc_id]++;
					if(rfnm_rx_chain_print_ok()) {
						pr_unflood(3, "phytimer chain break on adc %d: got %u expected %u (cc %u tail %d s %d)\n", la_adc_id,
							la_phytimer, rfnm_dev->rx_la_cb.expected_phytimer[la_adc_id], la_adc_cc, la_tail, s);
					}
				}
				rfnm_dev->rx_la_cb.expected_phytimer[la_adc_id] = la_phytimer + ((la_adc_size << rfnm_la9310_status->rx_r_shift) >> 1);
			}

			uint32_t usb_write_pos = rfnm_dev->rx_usb_cb.adc_buf_size[la_adc_id];

			// a packet must never contain a stamp discontinuity or epoch flip inside its
			// payload: force out the accumulated partial packet before appending such a sub
			uint32_t usb_seam_flush = usb_write_pos && (la_sub_discont ||
				la_sub_epoch != RFNM_STREAM_FLAG_EPOCH(rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].rx_flags));

			if((usb_write_pos + la_adc_size) > RFNM_RX_USB_BUF_SIZE_ELEMS || usb_seam_flush) {

				if((usb_write_pos /*+ la_adc_size*/) != RFNM_RX_USB_BUF_SIZE_ELEMS && !usb_seam_flush) {
					printk("!= RFNM_RX_USB_BUF_SIZE_ELEMS %d, %d\n", usb_write_pos, la_adc_size);
				}

				struct usb_ep_queue_ele *usb_ep_queue_ele;
				
				
				int stage_gated = rfnm_usb_rx_stage_gated();

				if (stage_gated) {
					// bound the in-flight backlog (keeps stream-start drain time rate-
					// independent) AND depth-limit staging for a stale consumer: unbounded
					// staging for a consumer that stopped reading kept a full queue of
					// armed-but-never-completing requests on the IN endpoints between
					// sessions (see rfnm_usb_rx_consumer_alive / RFNM_USB_INFLIGHT_STALE_CAP)
					usb_ep_queue_ele = NULL;
				} else {
					spin_lock(&rfnm_usb_req_buffer_in->list_lock);
					usb_ep_queue_ele = list_first_entry_or_null(&rfnm_usb_req_buffer_in->active, struct usb_ep_queue_ele, head);
					spin_unlock(&rfnm_usb_req_buffer_in->list_lock);
				}
				if (usb_ep_queue_ele == NULL && rfnm_usb_rx_consumer_alive()) {
					// Anomaly trace for a LIVE consumer only. Any refusal with a dead/absent
					// consumer is expected steady state (stale depth cap, or a pool emptied
					// by a lost enumeration) and is already counted + DISCONT-flagged;
					// printing it per drop turned a dead 61.44M session into a printk storm
					// from the SCHED_FIFO producer that starved the network off the SoC.
					pr_warn_ratelimited("RFNM: usb stage refused (%s, inflight %d)\n",
						stage_gated ? "gated" : "pool empty", atomic_read(&rfnm_usb_inflight_cnt));
				}

				if(usb_ep_queue_ele == NULL) {
					// no free gadget request: this packet is dropped. Flag the seam so the next
					// shipped packet carries RFNM_RX_FLAG_DISCONT (same contract as the LA-ring
					// catch-up drops) - with ship-time cc the client sees a flagged stamp jump,
					// never an unflagged cc hole.
					if(stage_gated) {
						rfnm_rx18.full_gated++;
					} else {
						rfnm_rx18.full_pool_empty++;
					}
					rfnm_stream_stats.usb_rx_error[0]++;
					rfnm_rx_jump_discont_usb |= (1 << la_adc_id);
				} else {
					// variable-size packets: elem_cnt declares the valid sample prefix and req->length
					// shortens the wire transfer to match (full packet here: usb_write_pos == ELEM_CNT)
					// ship-time cc: the wire chain stays contiguous across device-side drops
					rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].usb_cc = ++rfnm_dev->rx_usb_cb.usb_cc[la_adc_id];
					rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].elem_cnt = usb_write_pos;
					rfnm_usb_req_point_at_ring(usb_ep_queue_ele->req, (uint8_t *) &rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]]);
					usb_ep_queue_ele->req->length = RFNM_USB_RX_PACKET_HEAD_SIZE + usb_write_pos * 3;
					usb_ep_queue_ele->req->zero = 1;
					rfnm_rx_seam_ship_check(la_adc_id, 0, usb_ep_queue_ele);

					//printk("scheduling\n");


					//printk("Q %lx\n", usb_ep_queue_ele->req->buf);
					
						



					


					struct usb_ep_queue_ele usb_ep_queue_ele_tmp;

					//usb_ep_queue_ele_tmp = kzalloc(sizeof(struct usb_ep_queue_ele), GFP_KERNEL);

					memcpy(&usb_ep_queue_ele_tmp, usb_ep_queue_ele, sizeof(struct usb_ep_queue_ele ));

					spin_lock(&rfnm_usb_req_buffer_in_usb->list_lock);
					spin_lock(&rfnm_usb_req_buffer_in->list_lock);

					list_add_tail(&usb_ep_queue_ele->head, &rfnm_usb_req_buffer_in_usb->active);

					list_del(&usb_ep_queue_ele_tmp.head);

					spin_unlock(&rfnm_usb_req_buffer_in->list_lock);
					spin_unlock(&rfnm_usb_req_buffer_in_usb->list_lock);

					//kfree(usb_ep_queue_ele_tmp);

					//kfree(usb_ep_queue_ele);

					
					wake_up(&wq_usb);
					//tasklet_schedule(&rfnm_tasklet_usb);

					rfnm_stream_stats.usb_rx_ok[0]++;
					rfnm_rx18.full_ok++;
				}
				

				//spin_lock(&rfnm_dev->rx_usb_cb.reader_lock);

				rfnm_dev->rx_usb_cb.adc_buf_size[la_adc_id] = 0;
				usb_write_pos = 0;
				// advance the ring ONLY when the slot was handed to a request. On a drop the
				// slot is reused for the next packet: advancing here made every drop burn a
				// fresh slot, and a parked host (session start, reap stall) let head sweep
				// the 80-slot ring over the <=64 buffers still owned by in-flight requests,
				// rewriting them under dwc3 - the host reaped torn packets (a later packet's
				// header under the older request's queued length), duplicate ccs and
				// cc-contiguous stamp breaks.
				if(usb_ep_queue_ele != NULL) {
					rfnm_dev->rx_usb_cb.adc_buf[la_adc_id] = rfnm_dev->rx_usb_cb.head;

					if(++rfnm_dev->rx_usb_cb.head == RFNM_RX_USB_BUF_SIZE) {
						rfnm_dev->rx_usb_cb.head = 0;
					}
				}
			}
	#if 1
			//if(q == 0 && rfnm_dev->rx_usb_cb.adc_buf[la_adc_id] == 0)
			//printk("adc_buf %d offset %d destbuf %lx srcbuf %lx\n", rfnm_dev->rx_usb_cb.adc_buf[la_adc_id], LA_RX_BASE_BUFSIZE_12 * rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id], 
			//	&rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].buf[LA_RX_BASE_BUFSIZE_12 * rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id]], rfnm_bufdesc_rx[la_tail].buf);
	#endif

	#if 1
			//if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_4);
			//rfnm_pack16to12_aarch64_wrapper( (uint8_t *) &rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].buf[LA_RX_BASE_BUFSIZE_12 * rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id]], 
			//			(uint8_t *) rfnm_bufdesc_rx[la_tail].buf, 
			//			LA_RX_BASE_BUFSIZE / 1);
			

			//printk("memcpy usb_write_pos %d %x %x\n", usb_write_pos, 
			//(uint8_t *) &rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].buf[ 3 * usb_write_pos ], 
			//			(uint8_t *) &rfnm_bufdesc_rx[la_tail].buf[ 3 * sub_read_size ]);
			
			
			// head grace counts as a consumer here too: the packets shipped between the
			// stream start and the client's first completed read must carry REAL payload,
			// not whatever the ring slot held last session (headers are written at packet
			// birth regardless, so an elided copy ships stale IQ under fresh stamps)
			if(rfnm_usb_rx_consumer_alive() || rfnm_usb_rx_head_grace()) {
				memcpy( (uint8_t *) &rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].buf[ 3 * usb_write_pos ],
						(uint8_t *) &tmp_buff_uncompress[ 3 * sub_read_size ],
						la_adc_size * 3);
			}
/* *   buf_16b[n][i][j] = ((buf_12b[n][(i-18)*3 + 0][j] & 0xF) << 4) |
 *                     ((buf_12b[n][(i-18)*3 + 1][j] & 0xF) << 8) |
 *                     ((buf_12b[n][(i-18)*3 + 2][j] & 0xF) << 12);*/

			//memset( (uint8_t *) &rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].buf[ 3 * usb_write_pos ], 
			//			0xf0, 
			//			la_adc_size * 3);

			//if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_4);	
	#endif



	//delay_wavedetct++;


			if(!usb_write_pos) {
				rfnm_rx_pkt_birth[la_adc_id] = ktime_get();
				rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].magic = 0x7ab8bd6f;
				rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].fmt = RFNM_PACKET_FMT_PACKED12;
				rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].phytimer = la_phytimer;
				// usb_cc is assigned at SHIP time (both ship sites), not at birth: a packet
				// dropped for want of a gadget request must not burn a cc, or the client sees
				// an unflagged hole it cannot tell apart from transport loss
				rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].adc_id = la_adc_id;
				rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].rx_flags =
						((la_sub_discont || (rfnm_rx_jump_discont_usb & (1 << la_adc_id))) ? RFNM_RX_FLAG_DISCONT : 0) | (la_sub_epoch << 8);
				rfnm_rx_jump_discont_usb &= ~(1 << la_adc_id);
			}

			rfnm_dev->rx_usb_cb.adc_buf_size[la_adc_id] += la_adc_size;
			// r5 partials: an EXACTLY-full packet has no in-band ship trigger (ship-full
			// fires on the NEXT sub's overflow test) and 20-slot drain cycles end on a
			// packet boundary by arithmetic (3 packets = exactly 20 slots) - it used to
			// strand until the next pass's flush, one nap late, counted "partial" at
			// full length (THE 2:1 ratio). Age it out now: the tail flush ships it at
			// the END OF THIS PASS via the normal ship path.
			// r6: rx_ship_slots early-ships by the same age-out - packets leave at the
			// configured fill instead of 80 slots (latency lever, see the param block).
			if(rfnm_dev->rx_usb_cb.adc_buf_size[la_adc_id] == RFNM_RX_USB_BUF_SIZE_ELEMS ||
					(rx_ship_slots > 0 && rfnm_dev->rx_usb_cb.adc_buf_size[la_adc_id] >=
						(uint32_t)rx_ship_slots * RFNM_LA9310_DMA_RX_SIZE)) {
				rfnm_rx_pkt_birth[la_adc_id] = 0;
			}























			// FIX: the payload copy below was gated on local-consumer liveness but
			// ship/birth/advance were not - and every stream-IO reset revives the producer
			// (rfnm_note_host_ctrl) BEFORE the new client's first poll and resets
			// rx_local_cb.head to 0, so the first ~8-19 containers of every session
			// shipped slots 0..N with payload never written this lap: stale pool content
			// under fresh stamps/cc (measured: rx18 deadship 19 with ZERO strays on a
			// fresh boot = cold slots, the same 19 turning into kprobe strays on session
			// history). The whole lane now runs only for a live consumer; while dead,
			// drop any partial (its payload predates the seam - same law as the
			// standdown block) and mark the seam so the first live packet carries DISCONT.
			if(!rfnm_local_rx_consumer_alive()) {
				rfnm_dev->rx_local_cb.adc_buf_size[la_adc_id] = 0;
				rfnm_rx_jump_discont_local |= (1 << la_adc_id);
				goto local_lane_done;
			}

			uint32_t local_write_pos = rfnm_dev->rx_local_cb.adc_buf_size[la_adc_id];

			// same seam rule as the usb path: no stamp discontinuity or epoch flip inside a packet
			uint32_t local_seam_flush = local_write_pos && (la_sub_discont ||
				la_sub_epoch != RFNM_STREAM_FLAG_EPOCH(rfnm_local_buf_rx[rfnm_dev->rx_local_cb.adc_buf[la_adc_id]].p.rx_flags));

			// r6-eth: rx_ship_slots caps the local/TCP packet fill exactly like the USB
			// early-ship - the 80-slot fill alone is 333 us at 61.44M, the dominant RX
			// air->host term. Local mmap consumers and the v2 eth wire both honor
			// elem_cnt (seam flushes have shipped partials here since r4), so a smaller
			// fill is a latency knob, not a format change. 0 = full packets (default).
			uint32_t local_full = RFNM_RX_USB_BUF_SIZE_ELEMS;
			if(rx_ship_slots > 0 && (uint32_t)rx_ship_slots * RFNM_LA9310_DMA_RX_SIZE < local_full) {
				local_full = (uint32_t)rx_ship_slots * RFNM_LA9310_DMA_RX_SIZE;
			}

			if((local_write_pos + la_adc_size) > local_full || local_seam_flush) {
				if((local_write_pos) != local_full && !local_seam_flush && rx_ship_slots <= 0) {
					printk("!= RFNM_RX_USB_BUF_SIZE_ELEMS %d, %d\n", local_write_pos, la_adc_size);
				}

				struct rfnm_local_buffer_queue_ele *rfnm_local_queue_ele;

				// FIX: plain locked pop-then-add. The old choreography added the ele
				// to in_user WHILE IT WAS STILL LINKED IN THE FREE LIST (the add
				// overwrites its list pointers) and then repaired the free list through
				// an UNLOCKED stack memcpy of the old pointers - a proven
				// ele+slot+cc atomic-loss race at full rate (log signature: idx strides
				// mirror cc strides). Popped here, the ele is privately owned
				// between the two locked ops; no nested locks needed.
				spin_lock(&rfnm_local_buffer_in->list_lock);
				rfnm_local_queue_ele = list_first_entry_or_null(&rfnm_local_buffer_in->active, struct rfnm_local_buffer_queue_ele, head);
				if(rfnm_local_queue_ele != NULL) {
					list_del(&rfnm_local_queue_ele->head);
				}
				spin_unlock(&rfnm_local_buffer_in->list_lock);

				if(rfnm_local_queue_ele == NULL) {
					rfnm_stream_stats.local_rx_error[0]++;
				} else {
					bool rx_pool_was_empty;

					// Instrumentation: the payload copy above is gated on
					// rfnm_local_rx_consumer_alive() but this ship path is not - a packet
					// shipping while the local consumer is dead carries whatever the pool
					// slot held last lap, under fresh stamps/cc. Count it and scan what is
					// actually inside before it ships (slots 0..N: head resets to 0 at
					// every stream-IO reset, so the exposure is the SAME slots each session).
					if(!rfnm_local_rx_consumer_alive()) {
						rfnm_rx18.local_deadship++;
						if(rfnm70_prints < 64) {
							// scan as CS16 I-samples regardless of the fmt field (the
							// question is "is the exposed payload silent", not its format);
							// bound to the CS16 capacity so a PACKED12 elem count cannot
							// walk past the slot
							int16_t *pp = (int16_t *) &rfnm_local_buf_rx[rfnm_dev->rx_local_cb.adc_buf[la_adc_id]].p.buf[0];
							uint32_t lim = local_write_pos > 40960 ? 40960 : local_write_pos;
							int32_t mx = 0, first_hot = -1;
							uint32_t si;
							for(si = 0; si < lim; si += 8) {
								int32_t av = pp[2 * si] < 0 ? -pp[2 * si] : pp[2 * si];
								if(av > mx) mx = av;
								if(first_hot < 0 && av > 3000) first_hot = si;
							}
							rfnm70_prints++;
							printk("RFNM70 deadship %llu: slot %u elem %u fmt %u max %d first_hot %d\n",
								rfnm_rx18.local_deadship, rfnm_dev->rx_local_cb.adc_buf[la_adc_id],
								local_write_pos, rfnm_dev->rx_local_cb.fmt[la_adc_id], mx, first_hot);
						}
					}

					// elem_cnt = the valid prefix; local mmap consumers honor it and the
					// r6-eth v2 wire sends exactly this many samples (fixed-record wire retired)
					rfnm_local_buf_rx[rfnm_dev->rx_local_cb.adc_buf[la_adc_id]].p.elem_cnt = local_write_pos;
					rfnm_local_buf_rx[rfnm_dev->rx_local_cb.adc_buf[la_adc_id]].p.fmt = rfnm_dev->rx_local_cb.fmt[la_adc_id];
					// cc reform: cc minted AT SHIP, not at birth - the USB path's own
					// law (see its birth comment above): a buffer dropped for any reason
					// must not burn a cc, or the client sees an unflagged hole it cannot
					// tell apart from transport loss
					rfnm_local_buf_rx[rfnm_dev->rx_local_cb.adc_buf[la_adc_id]].p.usb_cc = ++rfnm_dev->rx_local_cb.local_cc[la_adc_id];
					rfnm_local_queue_ele->addr = rfnm_dev->rx_local_cb.adc_buf[la_adc_id];

					spin_lock(&rfnm_local_buffer_in_user->list_lock);
					rx_pool_was_empty = list_empty(&rfnm_local_buffer_in_user->active);
					list_add_tail(&rfnm_local_queue_ele->head, &rfnm_local_buffer_in_user->active);
					rfnm_local_rx_queue_depth++;
					spin_unlock(&rfnm_local_buffer_in_user->list_lock);

					// edge-triggered: consumers (eth tx worker, local poll()) only sleep once
					// they have drained to empty, so waking on every produced buffer just
					// rescheduled already-running threads (~tens of kHz of resched IPIs at rate)
					if (rx_pool_was_empty) {
						wake_up_interruptible(&local_rx_poll);
					}

					rfnm_stream_stats.local_rx_ok[0]++;
				}
				
				rfnm_dev->rx_local_cb.adc_buf_size[la_adc_id] = 0;
				local_write_pos = 0;
				// (dead first assignment removed: it deref'd the queue ele, which is NULL
				// on the pool-empty path - a latent oops the higher early-ship rate would
				// have started hitting; the head is the real next-buffer source)
				rfnm_dev->rx_local_cb.adc_buf[la_adc_id] = rfnm_dev->rx_local_cb.head;

				if(++rfnm_dev->rx_local_cb.head == RFNM_IQFLOOD_LOCAL_RX_BUF_SIZE) {
					rfnm_dev->rx_local_cb.head = 0;
				}
			}
















			if(0){


				    /* constants from your driver: */
					const size_t max_usb_elems = RFNM_RX_USB_BUF_SIZE_ELEMS;       /* number of 12b‐samples slots per USB buffer */
					const size_t elem_bytes   = 3;                                 /* bytes per sample */
					const size_t tmp_bytes    = sizeof(tmp_buff_uncompress);       /* total bytes in your temp buffer */
					const size_t usb_bytes    = sizeof(rfnm_rx_usb_buf[0].buf);     /* size of one USB‐buffer’s .buf[] */
				
					/* current values: */
					size_t write_pos = usb_write_pos;                              /* rfnm_dev->rx_usb_cb.adc_buf_size[...] */
					size_t elems     = la_adc_size;                                /* number of sample‐slots to copy */
					size_t adc_id    = la_adc_id;                                  /* which channel */
				
					/* 1) ID in range? */
					if (adc_id >= ARRAY_SIZE(rfnm_dev->rx_usb_cb.adc_buf)) {
						pr_err("rfnm_handler_in: bad adc_id %zu\n", adc_id);
					}
				
					/* 2) fits in USB‐ring? */
					if (write_pos + elems > max_usb_elems) {
						pr_err("rfnm_handler_in: usb‐ring overflow: pos=%zu + elems=%zu > %zu\n",
							   write_pos, elems, max_usb_elems);
					}
				
					/* convert to bytes */
					size_t byte_off = write_pos * elem_bytes;
					size_t byte_len = elems     * elem_bytes;
				
					/* 3a) fits in the USB‐buffer’s .buf[] array? */
					if (byte_off + byte_len > usb_bytes) {
						pr_err("rfnm_handler_in: usb‐buf overflow: off+len=%zu > buf_size=%zu\n",
							   byte_off + byte_len, usb_bytes);
					}
				
					/* 3b) fits in tmp_buff_uncompress? */
					if ((sub_read_size + elems) * elem_bytes > tmp_bytes) {
						pr_err("rfnm_handler_in: tmp_buff overflow: (sub_read=%u + elems=%zu)*%zu = %zu > %zu\n",
							   sub_read_size, elems, elem_bytes,
							   (sub_read_size + elems) * elem_bytes,
							   tmp_bytes);
					}





			}


			// unconditional: the lane gate above is THE liveness gate - re-checking here
			// could flip mid-sub and advance write_pos without the copy (the deadship class)
			{
				uint8_t *lpay = (uint8_t *) &rfnm_local_buf_rx[rfnm_dev->rx_local_cb.adc_buf[la_adc_id]].p.buf[0];
				if(!local_write_pos) {
					rfnm_dev->rx_local_cb.fmt[la_adc_id] = rfnm_local_rx_fmt;
				}
				if(rfnm_dev->rx_local_cb.fmt[la_adc_id] == RFNM_PACKET_FMT_CS16) {
					rfnm_local_fill_cs16(lpay + 4 * local_write_pos,
						(uint8_t *) &tmp_buff_uncompress[ 3 * sub_read_size ],
						la_adc_size);
				} else {
					memcpy(lpay + 3 * local_write_pos,
						(uint8_t *) &tmp_buff_uncompress[ 3 * sub_read_size ],
						la_adc_size * 3);
				}
			}


			if(!local_write_pos) {
				rfnm_local_buf_rx[rfnm_dev->rx_local_cb.adc_buf[la_adc_id]].p.magic = 0x7ab8bd6f;
				rfnm_local_buf_rx[rfnm_dev->rx_local_cb.adc_buf[la_adc_id]].p.phytimer = la_phytimer;
				// (cc reform: usb_cc now minted at SHIP, in the ship branch above -
				// birth keeps the time-domain truth (phytimer) and identity fields only)
				rfnm_local_buf_rx[rfnm_dev->rx_local_cb.adc_buf[la_adc_id]].p.adc_id = la_adc_id;
				rfnm_local_buf_rx[rfnm_dev->rx_local_cb.adc_buf[la_adc_id]].p.rx_flags =
						((la_sub_discont || (rfnm_rx_jump_discont_local & (1 << la_adc_id))) ? RFNM_RX_FLAG_DISCONT : 0) | (la_sub_epoch << 8);
				rfnm_rx_jump_discont_local &= ~(1 << la_adc_id);
			}

			rfnm_dev->rx_local_cb.adc_buf_size[la_adc_id] += la_adc_size;
			local_lane_done:;















				
			//static int print_delay = 0;
			//print_delay++;
			// cc chain validation (phase 1 hard requirement: DSP data loss must never be
			// transparent nor a fixed skewed offset), now mirroring the phytimer chain's
			// epoch/DISCONT rules EXACTLY. Only current-epoch subs are validated, and a
			// fw-FLAGGED discontinuity (DDR-write slot drop, self-heal re-gate - see
			// ddr_wr_drop_cnt) or a kernel-declared seam (stream start, jump-forward,
			// stand-down resume) re-anchors silently: the loss is already declared to
			// every consumer, and counting it per sub is what turned the dead-duplex
			// fw drop cycle (one 3-sub emission batch burned per busy DDR channel) into
			// a ~90k err/s mismatch storm. Unflagged breaks still count + print (budgeted).
			if(la_sub_epoch != (rfnm_la9310_status->rx_epoch & 0xFF)) {
				// stale-prefix drain: an old epoch's subs are not part of this chain -
				// skip validation and re-anchor when the current epoch's subs resume
				rfnm_rx_cc_anchor_pending |= (1 << la_adc_id);
			} else if((rfnm_rx_cc_anchor_pending & (1 << la_adc_id)) || la_sub_discont) {
				rfnm_rx_cc_anchor_pending &= ~(1 << la_adc_id);
				rfnm_dev->rx_la_cb.adc_cc[la_adc_id] = la_adc_cc + 1;
				rfnm_stream_stats.la_adc_ok[la_adc_id]++;
			} else if(rfnm_dev->rx_la_cb.adc_cc[la_adc_id] != la_adc_cc) {
				// unflagged chain break: count it, print within the per-stream budget, and
				// re-anchor on the observed cc (expected = got+1, so ONE fw-side hole is
				// ONE error - a persistent structural fault keeps counting in la_adc_error
				// but can no longer print unbounded)
				if(rfnm_rx_chain_print_ok()) {
					pr_unflood(2, "cc mismatch on adc %d -> %d vs %d tail is %d phytimer is %u s %d | adc_buf_size %d adc_buf %d head %d\n", la_adc_id,
						la_adc_cc, rfnm_dev->rx_la_cb.adc_cc[la_adc_id],
						la_tail, rfnm_bufdesc_rx[la_tail].bufdesc[s].phytimer, s,
						usb_write_pos, rfnm_dev->rx_usb_cb.adc_buf[la_adc_id], rfnm_dev->rx_usb_cb.head);
				}
				// first-fault forensics, once per stream: dump the previous + current slot
				// headers so a persistent fw-side chain fault (like the dead-duplex 3-of-6
				// cc burn) is diagnosable from dmesg instead of invisible behind ratelimits
				if(!rfnm_rx_slotdump_done) {
					uint32_t pt = la_tail ? la_tail - 1 : RFNM_ADC_BUFCNT - 1;

					rfnm_rx_slotdump_done = 1;
					for(int d = 0; d < RFNM_RX_BUF_OUT_SUB_CNT; d++) {
						printk("RFNM: ccdump slot %u sub %d: raw %08x cc %u pt %u size %u | slot %u sub %d: raw %08x cc %u pt %u size %u\n",
							pt, d, rfnm_bufdesc_rx[pt].bufdesc[d].adc_id, rfnm_bufdesc_rx[pt].bufdesc[d].cc,
							rfnm_bufdesc_rx[pt].bufdesc[d].phytimer, rfnm_bufdesc_rx[pt].bufdesc[d].size,
							la_tail, d, rfnm_bufdesc_rx[la_tail].bufdesc[d].adc_id, rfnm_bufdesc_rx[la_tail].bufdesc[d].cc,
							rfnm_bufdesc_rx[la_tail].bufdesc[d].phytimer, rfnm_bufdesc_rx[la_tail].bufdesc[d].size);
					}
					printk("RFNM: ccdump ctx: head %u tail %u s %d epoch %u r_shift %u regate %u\n",
						la_head, la_tail, s, rfnm_la9310_status->rx_epoch, rfnm_la9310_status->rx_r_shift, rfnm_la9310_status->rx_regate_cnt);
				}
				rfnm_dev->rx_la_cb.adc_cc[la_adc_id] = la_adc_cc + 1;

				rfnm_stream_stats.la_adc_error[la_adc_id]++;
			} else {

				rfnm_dev->rx_la_cb.adc_cc[la_adc_id]++;
				rfnm_stream_stats.la_adc_ok[la_adc_id]++;
			}
			// (each branch above owns its expected-cc update: the old unconditional
			// increment here would double-step the silent re-anchor and step the
			// counter even for skipped old-epoch subs)

			sub_read_size += la_adc_size;

		}
		if(++la_tail == RFNM_ADC_BUFCNT) {
			la_tail = 0;
		}
		
	}


	

	rfnm_dev->rx_la_cb.tail = la_tail;

	//rfnm_m7_status->kernel_cache_flush_tail = la_tail;

	
	

	//printk("head is at %d\n", rfnm_m7_status->rx_head);

exit_tasklet:
	//spin_unlock(&rfnm_dev->rx_usb_cb.reader_lock);
	// r5 partials fix: deadline flush AFTER the pass's drain work and BEFORE any nap
	// (flush-then-sleep). A fresh strand is younger than the deadline here (no-op);
	// after the nap the next pass DRAINS first, so the strand completes in-band and
	// ships full. Only genuine starvation (nothing arrived across a whole nap) ages
	// into the flush - which is what the deadline is for. The embedded stall
	// detectors see post-drain state, the same wedge signal.
	rfnm_rx_flush_partial_usb();
	if(rfnm_prod_nap_pending) {
		rfnm_prod_nap_pending = 0;
		if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_1);
		usleep_idle_range(500, 1000);
		if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_1);
	}
	

	
	

	if(rfnm_dev->wq_stop_in) {
		printk("stopping IN process\n");
		rfnm_dev->wq_stop_in = 0;
		atomic_dec(&rfnm_sm_workers_alive);
		return 0;
	}

}
}


// ==== windowed TX assembly (rt-cores step 2) ====
// Group contiguous slot-aligned TIME_VALID packets into windows; EOB closes one
// (the packet IS the schedule). The sink (lalib, dependency inversion like the
// other cbs) arms the rtc gate edges + paces the VSPA TX_WINDOW send. v1 is
// strict: ragged shapes (mid-slot tick / partial-slot content) refuse loudly.
int (*rfnm_tx_window_cb)(uint32_t open_tick, uint32_t src_slot, uint32_t len_slots);
EXPORT_SYMBOL_GPL(rfnm_tx_window_cb);
uint32_t (*rfnm_tx_windowed_q_cb)(void);
EXPORT_SYMBOL_GPL(rfnm_tx_windowed_q_cb);
uint32_t rfnm_wa_windows, rfnm_wa_ragged, rfnm_wa_implicit, rfnm_wa_cbfail;
EXPORT_SYMBOL_GPL(rfnm_wa_windows);
EXPORT_SYMBOL_GPL(rfnm_wa_ragged);
EXPORT_SYMBOL_GPL(rfnm_wa_implicit);
EXPORT_SYMBOL_GPL(rfnm_wa_cbfail);

static struct {
	uint32_t open;		// window open tick (slot-aligned by the strict check)
	uint32_t first_slot;
	uint32_t next_slot;	// expected next base_slot (contiguity)
	uint32_t slots;		// content slots accumulated
	uint32_t active;
} rfnm_tx_win;

static void rfnm_tx_win_close(int implicit)
{
	uint32_t pad_slot;
	int ret;

	if(!rfnm_tx_win.active) {
		return;
	}
	// the guaranteed-zero pad slot: post-purge, silence after content is RING
	// BYTES written here (one slot) - the close edge parks 64 samples into it
	pad_slot = rfnm_tx_win.next_slot;
	memset(&rfnm_bufdesc_tx[pad_slot], 0, sizeof(struct rfnm_bufdesc_tx));
	dcache_clean_poc(RFNM_CACHE_ADDR(&rfnm_bufdesc_tx[pad_slot]), RFNM_CACHE_ADDR(&rfnm_bufdesc_tx[pad_slot + 1]));
	ret = rfnm_tx_window_cb ?
			rfnm_tx_window_cb(rfnm_tx_win.open, rfnm_tx_win.first_slot, rfnm_tx_win.slots + 1) : -ENXIO;
	if(ret) {
		rfnm_wa_cbfail++;
		printk_ratelimited("RFNM: tx window sink failed (%d) - window dropped (open %u slots %u)\n",
				ret, rfnm_tx_win.open, rfnm_tx_win.slots);
	} else {
		rfnm_wa_windows++;
	}
	if(implicit) {
		rfnm_wa_implicit++;
	}
	rfnm_tx_win.active = 0;
}

int can_run_handler_out(void) {
	struct usb_ep_queue_ele *usb_ep_queue_ele;
	struct rfnm_local_buffer_queue_ele *rfnm_local_buffer_queue_ele;

	spin_lock(&rfnm_usb_req_buffer_out->list_lock);
	usb_ep_queue_ele = list_first_entry_or_null(&rfnm_usb_req_buffer_out->active, struct usb_ep_queue_ele, head);
	spin_unlock(&rfnm_usb_req_buffer_out->list_lock);

	spin_lock(&rfnm_local_buffer_out->list_lock);
	rfnm_local_buffer_queue_ele = list_first_entry_or_null(&rfnm_local_buffer_out->active, struct rfnm_local_buffer_queue_ele, head);
	spin_unlock(&rfnm_local_buffer_out->list_lock);

	return (usb_ep_queue_ele != NULL) || (rfnm_local_buffer_queue_ele != NULL);
}


/*
[  113.445429] N 0 16   33792
[  113.445897] Unable to handle kernel paging request at virtual address 000ff00f00000008
[  113.445908] Mem abort info:
[  113.445910]   ESR = 0x96000044
[  113.445912]   EC = 0x25: DABT (current EL), IL = 32 bits
[  113.445915]   SET = 0, FnV = 0
[  113.445918]   EA = 0, S1PTW = 0
[  113.445920]   FSC = 0x04: level 0 translation fault
[  113.445922] Data abort info:
[  113.445924]   ISV = 0, ISS = 0x00000044
[  113.445926]   CM = 0, WnR = 1
[  113.445928] [000ff00f00000008] address between user and kernel address ranges
[  113.445933] Internal error: Oops: 96000044 [#1] PREEMPT_RT SMP
[  113.445937] Modules linked in: rfnm_breakout(O) rfnm_usb_boost(O) rfnm_usb(O) rfnm_usb_function(O) rfnm_daughterboard(O) rfnm_lalib(O) la9310rfnm(O) rfnm_gpio(O) kpage_ncache(O) la9310shiva(O) overlay fsl_jr_uio caam_jr caamkeyblob_desc caamhash_desc caamalg_desc crypto_engine authenc libdes snd_soc_imx_hdmi crct10dif_ce dw_hdmi_cec snd_soc_fsl_xcvr caam secvio error fuse [last unloaded: rfnm_gpio]
[  113.445989] CPU: 2 PID: 938 Comm: TX Tainted: G        W  O      5.15.71-rt51 #358
[  113.445995] Hardware name: RFNM imx8mp (DT)
[  113.445998] pstate: 40000005 (nZcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[  113.446004] pc : rfnm_handler_out+0x234/0x360 [la9310rfnm]
[  113.446017] lr : rfnm_handler_out+0x210/0x360 [la9310rfnm]
[  113.446026] sp : ffff80001bc83dd0
[  113.446028] x29: ffff80001bc83dd0 x28: ffff80000138f3c0 x27: ffff0000d5678020
[  113.446035] x26: ffff80000138f010 x25: 000000000e040404 x24: 000000000d040304
[  113.446042] x23: ffff80000138c000 x22: 0000000000003241 x21: ffff0000c533f200
[  113.446049] x20: ffff0000d5678020 x19: 0000000000000840 x18: 0000000000000000
[  113.446055] x17: 0000000000000000 x16: 0000000000000000 x15: 00003584f5185d8e
[  113.446062] x14: 000000000000005d x13: 0000000000000001 x12: 0000000000000000
[  113.446069] x11: 0000000000000000 x10: 00000000000009d0 x9 : ffff80001bc83d10
[  113.446075] x8 : ffff0000c4ee6df0 x7 : ffff0000fb7e2c80 x6 : 0000000000000000
[  113.446082] x5 : 00000000006d7240 x4 : ffff0000c5a23d20 x3 : ffff0000c533f210
[  113.446088] x2 : ffff0000c5a23d20 x1 : f00ff00f00000000 x0 : f000f000f000f000
[  113.446096] Call trace:
*/

//static void rfnm_tasklet_handler_out(unsigned long tasklet_data) {
 int rfnm_handler_out(void * tasklet_data) {
//void rfnm_handler_out(struct work_struct * tasklet_data) {

	// 2 ms hard-lead (r5): the single ring writer runs SCHED_FIFO like its ingest
	// peer - a CFS wake tail on the dedicated core was tens-to-hundreds of us of
	// placement latency on this RT kernel
	struct sched_param sparam = { .sched_priority = 1 };
	sched_setscheduler(current, SCHED_FIFO, &sparam);

	while(1) {

	//	wait_event(wq_out, can_run_handler_out());


		struct usb_ep_queue_ele *usb_ep_queue_ele;
again:

		if(rfnm_dev->wq_stop_out) {
			printk("stopping OUT process\n");
			rfnm_dev->wq_stop_out = 0;
			atomic_dec(&rfnm_sm_workers_alive);
			return 0;
		}


		uint32_t list_size = 0;
		int cc_is_continuous = 0;

		// (USB OUT ingest moved to rfnm_handler_usb: the memcpy+inval per packet was
		// serialized with the ring unpack in this loop, capping the pair at ~3k pkt/s -
		// exactly 61.44M; 122.88M needs 6k. Now they pipeline across cores.)









/*local*/













		struct rfnm_local_buffer_queue_ele *rfnm_local_queue_ele;

		list_size = 0;
		cc_is_continuous = 0;

		// the pending list is kept cc-ordered at insert time: the head IS the next packet
		spin_lock(&rfnm_local_buffer_out->list_lock);
		rfnm_local_queue_ele = list_first_entry_or_null(&rfnm_local_buffer_out->active, struct rfnm_local_buffer_queue_ele, head);
		spin_unlock(&rfnm_local_buffer_out->list_lock);

		

		if(rfnm_local_queue_ele != NULL) {
			struct rfnm_tx_usb_buf *lb = &rfnm_local_buf_tx[rfnm_local_queue_ele->addr].p;
			// the local transport is one in-order TCP stream (single writer thread), so a
			// cc match can consume immediately at any queue depth - holding back packets
			// only adds latency. The USB multi-endpoint path keeps its reorder holdback.
			if(lb->usb_cc == rfnm_dev->tx_la_cb.usb_cc) {
				cc_is_continuous = 1;
			}
			if(lb->tx_flags & RFNM_TX_FLAG_TIME_VALID) {
				// timed packets are scheduled, not rate-fed: consume immediately regardless
				// of queue depth / cc lock. Otherwise sparse scheduled windows stall behind
				// the rate-fed flow-control holdback (list_size never exceeds the free pool,
				// cc isn't locked at stream start) - the pool fills and ingest deadlocks
				// before a single packet reaches the ring branch.
				cc_is_continuous = 1;
			}
			// 2 ms hard-lead (r5): POS packets carry their own placement identity - the
			// POS branch adopts cc and validates epoch/grid/lateness per packet, so the
			// reorder holdback (list_size > 8 after a cc gap) only converts ONE dropped
			// packet into ~8 guaranteed-late ones (~2 ms of feed held hostage). Consume
			// immediately, like TIME_VALID.
			if(lb->tx_flags & RFNM_TX_FLAG_POS_VALID) {
				cc_is_continuous = 1;
			}
		}

		// the resync hammer below needs the queue depth only on the cc-mismatch path -
		// counting every pass walked the whole pending list under the spinlock at packet rate
		if(rfnm_local_queue_ele != NULL && !cc_is_continuous) {
			spin_lock(&rfnm_local_buffer_out->list_lock);
			list_size = list_count_nodes(&rfnm_local_buffer_out->active);
			spin_unlock(&rfnm_local_buffer_out->list_lock);
		}

		// resync hammer: >2 queued with a cc mismatch (stream start, or after a loss) -
		// consume the head and let the cc-error path re-lock. The old ETH-test value (32)
		// deadlocked any client pacing shallower than 33 packets: nothing was consumed,
		// so the consumed-cc feedback in dev_status never advanced.
		if( ((list_size > 8 || rfnm_tx_consume_idle()) && rfnm_local_queue_ele != NULL) || (cc_is_continuous) ) {

//			uint32_t la_tail = rfnm_m7_status->tx_buf_id;
			if(!rfnm_tx_status_fresh()) {
				// fw status heartbeat stopped: the LA9310 is dead or rebooting. Refusing
				// to place data is the honest failure - the old extrapolator guessed on.
				printk_ratelimited("RFNM: fw status heartbeat stale, tx placement paused\n");
				// >1 s of CONTINUOUS stale heartbeat = the LA9310 is gone -
				// FAULT (the old 1/s stale-regate was the fourth autonomous heal). The
				// clock clears on every fresh read + at episode start: a block-local
				// static here carried a stale stamp ACROSS sessions and executed the
				// next session's first stale pass instantly (observed).
				if(!rfnm_tx_stale_since) {
					rfnm_tx_stale_since = jiffies;
				} else if(time_after(jiffies, rfnm_tx_stale_since + HZ)) {
					rfnm_session_fault("fw heartbeat stale");
				}
				usleep_idle_range(1000, 2000);
				continue;
			}
			rfnm_tx_stale_since = 0;


			// arm-lottery detector: verify the free-running pump once per stream episode
			// BEFORE consuming anything (see rfnm_tx_pace_check). Skipped once the ring is
			// legitimately parked by a timed schedule's quiesce.
			if(!rfnm_tx_pace_ok && rfnm_ptmr_now_cb && (rfnm_la9310_status->tx_state & 0x1)) {
				if(rfnm_tx_pace_check()) {
					usleep_idle_range(1000, 2000);
					continue;
				}
			}

			// The underrun realign died with the write head - a starved
			// stretch airs the scrubbed gap and the next packet self-places (that was
			// always the positional law; now it is the only law).

			



			// variable-size packets: the header says how many base bufs this packet carries
			uint32_t lb_multi;
			uint32_t lb_fmt;
			uint32_t lb_timed;
			{
				struct rfnm_tx_usb_buf *lb_hdr = &rfnm_local_buf_tx[rfnm_local_queue_ele->addr].p;
				lb_multi = (lb_hdr->multi >= 1 && lb_hdr->multi <= RFNM_TX_USB_BUF_MULTI) ? lb_hdr->multi : RFNM_TX_USB_BUF_MULTI;
				lb_fmt = lb_hdr->fmt;
				lb_timed = lb_hdr->tx_flags & RFNM_TX_FLAG_TIME_VALID;
			}

			// r6 transit-lag meter: how long this packet took from gadget completion to
			// this consume (split at the pool insert). The rare 30 ms batch-drain class
			// must show WHERE it waited; >5 ms prints loudly with the split.

			// THE judge: one comparison against the one clock,
			// both flag flavors. The u32 stamp extends by nearest-window against
			// phytimer_now64 (everything live sits within half a wrap of now). Late
			// drops loudly; EARLY IS NOT AN ERROR - the packet waits at the queue head
			// (ring-capacity backpressure computed from the clock, not tail estimates).
			{
				struct rfnm_tx_usb_buf *jb = &rfnm_local_buf_tx[rfnm_local_queue_ele->addr].p;
				uint32_t tps = 128u << rfnm_la9310_status->tx_r_shift;
				uint64_t now64 = rfnm_phytimer_now64();
				uint64_t stamp64 = (now64 & ~0xFFFFFFFFull) | jb->phytimer;
				int64_t lead;

				if(stamp64 > now64 && stamp64 - now64 > 0x80000000ull) {
					stamp64 -= 0x100000000ull;
				} else if(now64 > stamp64 && now64 - stamp64 > 0x80000000ull) {
					stamp64 += 0x100000000ull;
				}
				lead = (int64_t)stamp64 - (int64_t)now64;
				// placement-margin meter, honest basis: TRUE lead in slots at consume
				rfnm_tx_pos_gap_hist[min(fls((uint32_t)clamp_t(int64_t, lead / tps, 1, 0x7fffffff)), 15)]++;
				if(lead > 0 && (uint32_t)(lead / tps) < rfnm_tx_pos_gap_min) {
					rfnm_tx_pos_gap_min = (uint32_t)(lead / tps);
				}
				if(lead < (int64_t)rfnm_tx_min_lead_ticks(tps)) {
					if(lb_timed) {
						rfnm_tx_stat_timed_reject++;
					} else {
						rfnm_tx_stat_pos_late++;
					}
					rfnm_tx_last_late_usb_cc = jb->usb_cc;
					printk_ratelimited("RFNM: tx packet late (lead %lld ticks vs min %u) - dropped\n",
							(long long)lead, rfnm_tx_min_lead_ticks(tps));
					rfnm_dev->tx_la_cb.usb_cc = jb->usb_cc + 1;
					goto tx_consume_done;
				}
				if(lead > (int64_t)(RFNM_DAC_BUFCNT / 2) * tps) {
					if(lead > (int64_t)(RFNM_DAC_BUFCNT / 2) * tps + 61440000ll) {
						// a second beyond the ring span = a garbage stamp, not an early bird
						if(lb_timed) {
							rfnm_tx_stat_timed_reject++;
						} else {
							rfnm_tx_stat_pos_late++;
						}
						printk_ratelimited("RFNM: tx packet wild (lead %lld ticks) - dropped\n", (long long)lead);
						rfnm_dev->tx_la_cb.usb_cc = jb->usb_cc + 1;
						goto tx_consume_done;
					}
					usleep_idle_range(200, 500);
					continue;
				}
			}

			if(rfnm_local_queue_ele->born_kt) {
				ktime_t kt_c = ktime_get();
				int64_t lag_pool = ktime_us_delta(kt_c, rfnm_local_queue_ele->enq_kt);
				int64_t lag_ing = ktime_us_delta(rfnm_local_queue_ele->enq_kt, rfnm_local_queue_ele->born_kt);
				int64_t lag_tot = lag_pool + lag_ing;

				rfnm_tx_lag_ingest_hist[min(fls((uint32_t)clamp_t(int64_t, lag_ing, 1, 0x7fffffff)), 15)]++;
				rfnm_tx_lag_pool_hist[min(fls((uint32_t)clamp_t(int64_t, lag_pool, 1, 0x7fffffff)), 15)]++;
				if(lag_tot > 0 && (uint32_t)lag_tot > rfnm_tx_lag_max_us) {
					rfnm_tx_lag_max_us = (uint32_t)lag_tot;
				}
				if(lag_tot > 5000) {
					printk_ratelimited("RFNM: tx transit lag %lld us (ingest %lld pool %lld) cc %llu\n",
							lag_tot, lag_ing, lag_pool,
							(unsigned long long)rfnm_local_queue_ele->local_cc);
				}
			}

			// TIME_VALID = explicit absolute tick, ANY alignment, placed
			// DIRECTLY into the ring by stamp (the TX_SLOT walker lane is gone):
			// zero head-pad + offset copy + zero tail-pad; the retire fifo scrubs the
			// span after air (68 ms lap hygiene). ta_sweep-class writers UNCHANGED.
			if(lb_timed) {
				struct rfnm_tx_usb_buf *tbh = &rfnm_local_buf_tx[rfnm_local_queue_ele->addr].p;
				uint8_t *pl = (uint8_t *) tbh + RFNM_USB_TX_PACKET_HEAD_SIZE;
				uint32_t tps = 128u << rfnm_la9310_status->tx_r_shift;
				uint32_t off = tbh->phytimer - rfnm_la9310_status->tx_t0;
				uint32_t base_slot = (off / tps) & (RFNM_DAC_BUFCNT - 1);
				uint32_t elems_per_slot = LA_TX_BASE_BUFSIZE / 4;
				uint32_t pad_samples = (uint32_t)div_u64((uint64_t)(off % tps) * elems_per_slot, tps);
				uint32_t content = lb_multi * elems_per_slot;
				uint32_t span = (pad_samples + content + elems_per_slot - 1) / elems_per_slot;
				uint32_t w, done = 0;
				const uint8_t *csrc;

				dcache_inval_poc(RFNM_CACHE_ADDR(tbh),
					RFNM_CACHE_ADDR((uint8_t *) tbh + RFNM_USB_TX_PACKET_HEAD_SIZE +
						lb_multi * (lb_fmt == RFNM_PACKET_FMT_CS16 ? LA_TX_BASE_BUFSIZE : LA_TX_BASE_BUFSIZE_12)));
				if(lb_fmt == RFNM_PACKET_FMT_CS16) {
					csrc = pl;
				} else {
					for(w = 0; w < lb_multi; w++) {
						rfnm_unpack12to16_aarch64_wrapper(rfnm_tx_time_stage + (size_t)w * LA_TX_BASE_BUFSIZE,
								pl + (size_t)w * LA_TX_BASE_BUFSIZE_12, LA_TX_BASE_BUFSIZE);
					}
					csrc = rfnm_tx_time_stage;
				}
				for(w = 0; w < span; w++) {
					uint32_t ws = (base_slot + w) & (RFNM_DAC_BUFCNT - 1);
					uint8_t *dst = (uint8_t *) rfnm_bufdesc_tx[ws].buf;
					uint32_t s0 = (w == 0) ? pad_samples : 0;
					uint32_t n = min(elems_per_slot - s0, content - done);

					if(s0) {
						memset(dst, 0, (size_t)s0 * 4);
					}
					memcpy(dst + (size_t)s0 * 4, csrc + (size_t)done * 4, (size_t)n * 4);
					if(s0 + n < elems_per_slot) {
						memset(dst + (size_t)(s0 + n) * 4, 0, (size_t)(elems_per_slot - s0 - n) * 4);
					}
					dcache_clean_poc(RFNM_CACHE_ADDR(&rfnm_bufdesc_tx[ws]), RFNM_CACHE_ADDR(&rfnm_bufdesc_tx[ws + 1]));
					done += n;
				}
				// windowed assembly: contiguity + EOB own the window boundaries
				if(rfnm_tx_windowed_q_cb && rfnm_tx_windowed_q_cb()) {
					uint32_t aligned = (pad_samples == 0) && (span == lb_multi);

					if(rfnm_tx_win.active && (!aligned || base_slot != rfnm_tx_win.next_slot)) {
						// discontinuity: close what stands (counted - EOB is
						// the contract, this is the tolerant fallback)
						rfnm_tx_win_close(1);
					}
					if(!aligned) {
						rfnm_wa_ragged++;
						printk_ratelimited("RFNM: windowed: ragged timed packet (pad %u span %u multi %u) - not windowed\n",
								pad_samples, span, lb_multi);
					} else {
						if(!rfnm_tx_win.active) {
							rfnm_tx_win.open = tbh->phytimer;
							rfnm_tx_win.first_slot = base_slot;
							rfnm_tx_win.next_slot = base_slot;
							rfnm_tx_win.slots = 0;
							rfnm_tx_win.active = 1;
						}
						rfnm_tx_win.next_slot = (rfnm_tx_win.next_slot + lb_multi) & (RFNM_DAC_BUFCNT - 1);
						rfnm_tx_win.slots += lb_multi;
						if(tbh->tx_flags & RFNM_TX_FLAG_EOB) {
							rfnm_tx_win_close(0);
						}
					}
				}
				rfnm_tx_stat_timed_placed++;
				rfnm_dev->tx_la_cb.usb_cc = tbh->usb_cc + 1;
				rfnm_tx_last_consume_jiffies = jiffies;
				rfnm_stream_stats.local_tx_ok[0]++;
				goto tx_consume_done;
			}


			// positional free-run: the stamp IS the placement - no head
			// accident, no align races. Every packet resolves to its slot off the live
			// anchor; the ONLY drops are contract violations, each counted + loud.
			{
				struct rfnm_tx_usb_buf *pb = &rfnm_local_buf_tx[rfnm_local_queue_ele->addr].p;

				if(pb->tx_flags & RFNM_TX_FLAG_POS_VALID) {
					uint32_t tps = 128u << rfnm_la9310_status->tx_r_shift;
					uint32_t off = pb->phytimer - rfnm_la9310_status->tx_t0;
					uint32_t slot = (off / tps) & (RFNM_DAC_BUFCNT - 1);
					uint8_t *pb_payload = (uint8_t *) pb + RFNM_USB_TX_PACKET_HEAD_SIZE;
					int w;

					if(pb->usb_cc != rfnm_dev->tx_la_cb.usb_cc) {
						rfnm_tx_stat_cc_gap++;
						rfnm_dev->tx_la_cb.usb_cc = pb->usb_cc;
					}
					rfnm_dev->tx_la_cb.usb_cc++;
					rfnm_tx_last_consume_jiffies = jiffies;

					if(rfnm_pos_tx_viable_cb && !rfnm_pos_tx_viable_cb() &&
							!(rfnm_tdd_ch_present_cb && rfnm_tdd_ch_present_cb())) {
						// P3/D3: a pure TDD pattern owns the drain (VSPA paces to
						// the duty, tx_epoch re-mints per window) - positional
						// placement is structurally impossible. Refuse loudly
						// instead of letting stamps rot as mis-judged lates.
						rfnm_tx_stat_mode_reject++;
						printk_ratelimited("RFNM: positional tx refused: TDD pattern owns the drain (arm the schedule ring for port-TDD positional TX)\n");
						goto tx_consume_done;
					}
					// THE TX STAMP AXIS decision:
					// the epoch check that lived here is DELETED. The tick is a wall
					// tick - the lib mints it as anchor_t0 + feed_pos*R, which names an
					// absolute wall moment that survives every re-mint; "maps to garbage
					// on the fresh grid" was false (slot = (tick - live_t0)/tps above IS
					// the fresh-grid resolution, done per packet). A tick whose wall
					// moment died in a drain stall lands in the late/wild check below
					// and drops HONESTLY. tx_flags[15:8] is reserved-zero from the v6
					// fleet wave; until then old writers stamp it and it is ignored.
					// rfnm_tx_stat_pos_stale stays declared as a tombstone (module
					// param ABI) and must read 0 forever.
					if(off % tps) {
						rfnm_tx_stat_pos_misaligned++;
						printk_ratelimited("RFNM: pos tx packet off-grid (off %u %% %u) - dropped\n", off, tps);
						goto tx_consume_done;
					}
					// The gap-vs-tail late/wild check died - THE judge above
					// already ruled on this stamp against phytimer_now64.

					for(w = 0; w < lb_multi; w++) {
						uint32_t ws = (slot + w) & (RFNM_DAC_BUFCNT - 1);
						if(lb_fmt == RFNM_PACKET_FMT_CS16) {
							memcpy((uint8_t *) rfnm_bufdesc_tx[ws].buf,
									pb_payload + (w * LA_TX_BASE_BUFSIZE), LA_TX_BASE_BUFSIZE);
						} else {
							rfnm_unpack12to16_aarch64_wrapper((uint8_t *) rfnm_bufdesc_tx[ws].buf,
									pb_payload + (w * LA_TX_BASE_BUFSIZE_12), LA_TX_BASE_BUFSIZE);
						}
					}
					if(slot + lb_multi > RFNM_DAC_BUFCNT) {
						dcache_clean_poc(RFNM_CACHE_ADDR(&rfnm_bufdesc_tx[slot]), RFNM_CACHE_ADDR(&rfnm_bufdesc_tx[RFNM_DAC_BUFCNT]));
						dcache_clean_poc(RFNM_CACHE_ADDR(&rfnm_bufdesc_tx[0]), RFNM_CACHE_ADDR(&rfnm_bufdesc_tx[slot + lb_multi - RFNM_DAC_BUFCNT]));
					} else {
						dcache_clean_poc(RFNM_CACHE_ADDR(&rfnm_bufdesc_tx[slot]), RFNM_CACHE_ADDR(&rfnm_bufdesc_tx[slot + lb_multi]));
					}

					rfnm_tx_pos_expect = (slot + lb_multi) & (RFNM_DAC_BUFCNT - 1);
					rfnm_tx_pos_expect_valid = 1;
					// tx_la_cb.head is VESTIGIAL kernel bookkeeping (the fw's TX pump
					// is open-loop: it fetches by its own tx_buf_id and never reads a
					// host cursor - proven against the fw-side map). Kept advancing only
					// so the debugfs tx_ring_head line stays a sane write-frontier
					// indicator.
					rfnm_dev->tx_la_cb.head = rfnm_tx_pos_expect;


					rfnm_tx_stat_pos_placed++;
					rfnm_stream_stats.local_tx_ok[0]++;
					goto tx_consume_done;
				}
			}

			// No third lane. Every legitimate writer stamps (the lib always
			// does); an unstamped packet is a protocol violation - counted, dropped loud.
			{
				struct rfnm_tx_usb_buf *ub = &rfnm_local_buf_tx[rfnm_local_queue_ele->addr].p;

				rfnm_tx_stat_unstamped++;
				printk_ratelimited("RFNM: unstamped tx packet (flags %x cc %llu) - dropped (no free-run lane)\n",
						ub->tx_flags, (unsigned long long)ub->usb_cc);
				rfnm_dev->tx_la_cb.usb_cc = ub->usb_cc + 1;
				rfnm_tx_last_consume_jiffies = jiffies;
			}

tx_consume_done:;

/*
			struct usb_ep_queue_ele usb_ep_queue_ele_tmp;

			memcpy(&usb_ep_queue_ele_tmp, usb_ep_queue_ele, sizeof(struct usb_ep_queue_ele));

			spin_lock(&rfnm_usb_req_buffer_out_usb->list_lock);
			spin_lock(&rfnm_usb_req_buffer_out->list_lock);

			list_add_tail(&usb_ep_queue_ele->head, &rfnm_usb_req_buffer_out_usb->active);
			
			list_del(&usb_ep_queue_ele_tmp.head);

			spin_unlock(&rfnm_usb_req_buffer_out_usb->list_lock);
			spin_unlock(&rfnm_usb_req_buffer_out->list_lock);

			wake_up(&wq_usb);
*/



			
			struct rfnm_local_buffer_queue_ele rfnm_local_queue_ele_tmp;
			bool tx_pool_was_empty;

			memcpy(&rfnm_local_queue_ele_tmp, rfnm_local_queue_ele, sizeof(struct rfnm_local_buffer_queue_ele));

			spin_lock(&rfnm_local_buffer_out->list_lock);
			spin_lock(&rfnm_local_buffer_out_user->list_lock);

			tx_pool_was_empty = list_empty(&rfnm_local_buffer_out_user->active);
			list_add_tail(&rfnm_local_queue_ele->head, &rfnm_local_buffer_out_user->active);

			list_del(&rfnm_local_queue_ele_tmp.head);

			spin_unlock(&rfnm_local_buffer_out->list_lock);
			spin_unlock(&rfnm_local_buffer_out_user->list_lock);

			// edge-triggered: writers (eth ingest, local write()) only wait once the free
			// pool is exhausted, so only the empty->nonempty transition needs a wake
			if (tx_pool_was_empty) {
				wake_up_interruptible(&local_tx_poll);
			}


			rfnm_stream_stats.local_tx_ok[0]++;
			goto again;
		}














		

		
		
		
		if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_3);
		// 2 ms hard-lead (r5): event-driven wake replaces the blind 500-1000 us nap -
		// the nap was the single largest TX pipeline term (every write burst arrived
		// into an empty queue and waited a full nap before placement). The producers
		// (eth ingest, local write, USB ingest handoff) now wake wq_out on the
		// empty->nonempty edge; the timeout keeps the old polling as a fallback for
		// the paths that have no event source (fw tail progress).
		wait_event_idle_timeout(wq_out, can_run_handler_out(), usecs_to_jiffies(1000));
		if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_3);
	}

	return 0;
}



void callback_func(struct device *dev)
{
	printk("Legacy RFNM callback function\n");
}

static irqreturn_t callback_func_0(int irq, void *dev) {
	callback_func(dev);
	return IRQ_HANDLED;
}

static irqreturn_t callback_func_1(int irq, void *dev) {
	callback_func(dev);
	return IRQ_HANDLED;
}
/*
int rfnm_callback_init(struct la9310_dev *la9310_dev)
{
	int ret = 0;
	printk("RFNM Callback registered\n");
	ret = register_rfnm_callback((void *)callback_func_0, 0);
	ret = register_rfnm_callback((void *)callback_func_1, 1);
	//last_print_time = ktime_get();

	return ret;
}
*/
/*int unregister_rfnm_callback(void );
int rfnm_callback_deinit(void)
{
	int ret = 0;
	ret = unregister_rfnm_callback();
	return ret;
}
*/




void rfnm_submit_usb_req_in(struct usb_ep *ep, struct usb_request *req)
{
	struct usb_composite_dev	*cdev;
	struct f_sourcesink		*ss = ep->driver_data;
	int				status = req->status;

	struct usb_ep_queue_ele *new_ele;
	unsigned long flags;

	// Accounting FIRST, unconditionally: every completion must return its tracked
	// ele to the pool. The old early returns below (!ss on a disabled ep, and
	// -ESHUTDOWN) leaked one inflight slot per event - and a session (re)open
	// disables the endpoints WITH requests in flight, so every open leaked a
	// batch. The gate then counted phantom inflight forever, staging stopped, and
	// RX shipping died while dwc3 sat idle: the USB duplex wedge's true root.
	// (Counter and list leaked TOGETHER, which is why the divergence self-check
	// never fired.)
	if (!rfnm_usb_req_buffer_in || !rfnm_usb_req_inflight_in) {
		return;	// completion before the stream lists exist (enumeration/teardown)
	}
	new_ele = req->context;
	if (new_ele) {
		req->context = NULL;
		spin_lock_irqsave(&rfnm_usb_req_inflight_in->list_lock, flags);
		list_del(&new_ele->head);
		spin_unlock_irqrestore(&rfnm_usb_req_inflight_in->list_lock, flags);
		atomic_dec(&rfnm_usb_inflight_cnt);
	} else {
		new_ele = kzalloc(sizeof(struct usb_ep_queue_ele), GFP_ATOMIC);
		if (!new_ele) {
			return;
		}
		rfnm_rx18.req_created++;
	}
	/* driver_data null = ep disabled: same teardown rule as -ESHUTDOWN below.
	 * rfnm_usb_req_free, NOT free_ep_req: a shipped request's buf points into the
	 * memremap'd RX carveout and kfree'ing that corrupts the allocator. */
	if (!ss) {
		rfnm_rx18.req_destroyed++;
		rfnm_usb_req_free(ep, req);
		kfree(new_ele);
		return;
	}

	//*gpio4 = *gpio4 | (0x1 << 1); *gpio4 = *gpio4 & ~(0x1 << 1);


	switch (status) {

	case 0:				/* normal completion? */

		/* consumer recency is stamped HERE and only here: a status-0 completion is
		 * the one event that proves a host is actually reading. Error givebacks
		 * (dequeue drains, dwc3 stall-work reclaim, teardown) used to refresh this
		 * stamp too, which kept the producer+stager feeding a dead session forever
		 * (see rfnm_usb_rx_consumer_alive). The same proof of life retires the
		 * new-session head grace for good: from here on, recency alone governs the
		 * staging depth, so a consumer that dies MID-stream still falls back to the
		 * stale cap (see rfnm_usb_rx_head_grace). */
		WRITE_ONCE(rfnm_usb_last_submit_jiffies, jiffies ? jiffies : 1);
		WRITE_ONCE(rfnm_usb_head_grace_until, 0);

		atomic_inc(&rfnm_usb_in_done_cnt);	// rate-trigger intake
		rfnm_ep_stats[RFNM_USB_EP_OK]++;

		// rx seam tripwire: this request just finished transmitting. If the staging
		// slot's header no longer matches the ship-time snapshot, the producer rewrote
		// a slot still owned by an in-flight request (the head-sweep class documented
		// at the ring-advance guard) - the wire may have carried a torn packet.
		if (new_ele->seam_cc && rfnm_usb_buf_in_rx_carveout(req->buf)) {
			struct rfnm_rx_usb_buf *seam_pkt = (struct rfnm_rx_usb_buf *) req->buf;
			if (seam_pkt->usb_cc != new_ele->seam_cc || seam_pkt->phytimer != new_ele->seam_pt) {
				rfnm_rx_seam.inflight_overwrite++;
				pr_unflood(3, "rx SEAM inflight overwrite: shipped cc %llu pt %u, completed cc %llu pt %u\n",
					(unsigned long long) new_ele->seam_cc, new_ele->seam_pt,
					(unsigned long long) seam_pkt->usb_cc, seam_pkt->phytimer);
			}
		}
		new_ele->seam_cc = 0;
		//printk("req->length %d\n", req->length);

		//if (ep == ss->out_ep[0]) {
			//check_read_data(ss, req);
			//if (ss->pattern != 2)
			//	memset(req->buf, 0x55, req->length);
		//}
		break;

	/* this endpoint is normally active while we're configured */
	case -ECONNABORTED:		/* hardware forced ep reset */
	case -ECONNRESET:		/* request dequeued: host-side libusb timeout/cancel. The slot must be
					 * recycled into the pool - returning here permanently leaked one
					 * request per host timeout; at low sample rates (slow stream start)
					 * the leaks cascaded until the pool was extinct = RX starvation
					 * below ~15M. Fall through to the re-add block. */
		rfnm_ep_stats[RFNM_USB_EP_DEAD]++;
		break;
	case -ESHUTDOWN:		/* endpoint teardown: the req dies with it. The inflight
					 * accounting already happened at entry (the leak fix); pooling
					 * this ele would hand the stager a stale request and crash -
					 * free both, the next enable allocates fresh ones.
					 * rfnm_usb_req_free, NOT free_ep_req: a shipped request's buf
					 * points into the memremap'd RX carveout - kfree'ing that
					 * corrupts the allocator. */
		printk_ratelimited("%s dead (%d), %d/%d\n", ep->name, status, req->actual, req->length);
		rfnm_ep_stats[RFNM_USB_EP_DEAD]++;
		rfnm_rx18.req_destroyed++;
		rfnm_usb_req_free(ep, req);
		kfree(new_ele);
		return;

	case -EOVERFLOW:		/* buffer overrun on read means that
					 * we didn't provide a big enough
					 * buffer.
					 */
		rfnm_ep_stats[RFNM_USB_EP_OVERFLOW]++;
		printk_ratelimited("%s EOVERFLOW (%d), %d/%d\n", ep->name, status, req->actual, req->length);

	default:
#if 1
		rfnm_ep_stats[RFNM_USB_EP_DEFAULT]++;
		printk_ratelimited("%s complete --> %d, %d/%d\n", ep->name, status, req->actual, req->length);
		break;
#endif
	case -EREMOTEIO:		/* short read */
		rfnm_ep_stats[RFNM_USB_EP_REMOTEIO]++;
		printk( "%s short read (%d), %d/%d\n", ep->name, status, req->actual, req->length);
		break;
	}
#if 1
rfnm_readd:
	// Learn the IN eps for the stream-IO-reset pool top-up. Read-mostly
	// fast path (entries are write-once, the count only grows); lock only to add.
	{
		int i;

		for(i = 0; i < rfnm_usb_in_ep_cnt && rfnm_usb_in_eps[i] != ep; i++) {}
		if(i == rfnm_usb_in_ep_cnt) {
			unsigned long f2;

			spin_lock_irqsave(&rfnm_usb_in_ep_lock, f2);
			for(i = 0; i < rfnm_usb_in_ep_cnt && rfnm_usb_in_eps[i] != ep; i++) {}
			if(i == rfnm_usb_in_ep_cnt && i < RFNM_USB_IN_EPS_MAX) {
				rfnm_usb_in_eps[i] = ep;
				rfnm_usb_in_ep_cnt = i + 1;
			}
			spin_unlock_irqrestore(&rfnm_usb_in_ep_lock, f2);
		}
	}
	// pool re-add LAST (after every req field read): re-adding earlier let the
	// stager on another CPU reuse the request while this handler still ran
	new_ele->ep = ep;
	new_ele->req = req;
	spin_lock_irqsave(&rfnm_usb_req_buffer_in->list_lock, flags);
	list_add_tail(&new_ele->head, &rfnm_usb_req_buffer_in->active);
	spin_unlock_irqrestore(&rfnm_usb_req_buffer_in->list_lock, flags);

	//static int wg_delay = 0;

	//wake_up(&wq_in);

	//tasklet_schedule(&rfnm_tasklet);

#else 

status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		printk("kill %s:  resubmit %d bytes --> %d\n", ep->name, req->length, status);
		rfnm_usb_ep_halt_guarded(ep, status);
		// FIXME recover later ... somehow 
	}

#endif

	// actual was working before... what changed?
	rfnm_stream_stats.usb_rx_bytes[0] += req->actual;





	/*status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		printk("kill %s:  resubmit %d bytes --> %d\n", ep->name, req->length, status);
		rfnm_usb_ep_halt_guarded(ep, status);
		// FIXME recover later ... somehow 
	}*/
}

EXPORT_SYMBOL_GPL(rfnm_submit_usb_req_in);








void rfnm_submit_usb_req_out(struct usb_ep *ep, struct usb_request *req)
{
	struct usb_composite_dev	*cdev;
	struct f_sourcesink		*ss = ep->driver_data;
	int				status = req->status;

	/* driver_data will be null if ep has been disabled */
	if (!ss)
		return;

	//*gpio4 = *gpio4 | (0x1 << 1); *gpio4 = *gpio4 & ~(0x1 << 1);


	switch (status) {

	case 0:				/* normal completion? */

		rfnm_ep_stats[RFNM_USB_EP_OK]++;
		//printk("req->length %d\n", req->length);

		//if (ep == ss->out_ep[0]) {
			//check_read_data(ss, req);
			//if (ss->pattern != 2)
			//	memset(req->buf, 0x55, req->length);
		//}
		break;

	/* this endpoint is normally active while we're configured */
	case -ECONNABORTED:		/* hardware forced ep reset */
	case -ECONNRESET:		/* request dequeued: host-side libusb timeout/cancel. The slot must be
					 * recycled into the pool - returning here permanently leaked one
					 * request per host timeout; at low sample rates (slow stream start)
					 * the leaks cascaded until the pool was extinct = RX starvation
					 * below ~15M. Fall through to the re-add block. */
		rfnm_ep_stats[RFNM_USB_EP_DEAD]++;
		break;
	case -ESHUTDOWN:		/* disconnect from host / endpoint teardown (ratelimited: the
					 * session-boundary rearm retires a whole queue of these at once,
					 * partly under the rearm spinlock) */
		printk_ratelimited("%s dead (%d), %d/%d\n", ep->name, status, req->actual, req->length);
		rfnm_ep_stats[RFNM_USB_EP_DEAD]++;

		//if (ep == ss->out_ep[0])
			//check_read_data(ss, req);
		free_ep_req(ep, req);
		return;

	case -EOVERFLOW:		/* buffer overrun on read means that
					 * we didn't provide a big enough
					 * buffer.
					 */
		rfnm_ep_stats[RFNM_USB_EP_OVERFLOW]++;
		printk_ratelimited("%s EOVERFLOW (%d), %d/%d\n", ep->name, status, req->actual, req->length);

	default:
#if 1
		rfnm_ep_stats[RFNM_USB_EP_DEFAULT]++;
		printk_ratelimited("%s complete --> %d, %d/%d\n", ep->name, status, req->actual, req->length);
		break;
#endif
	case -EREMOTEIO:		/* short read */
		rfnm_ep_stats[RFNM_USB_EP_REMOTEIO]++;
		printk( "%s short read (%d), %d/%d\n", ep->name, status, req->actual, req->length);
		break;
	}
#if 1
	struct usb_ep_queue_ele *new_ele;
	struct rfnm_tx_usb_buf *lb = req->buf;

	new_ele = kzalloc(sizeof(struct usb_ep_queue_ele), GFP_KERNEL);
	new_ele->ep = ep;
	new_ele->req = req;
	new_ele->usb_cc = lb->usb_cc;
	new_ele->born_kt = ktime_get();
	//new_ele->dwc_queue_sent = 0;

	unsigned long flags;

	spin_lock_irqsave(&rfnm_usb_req_buffer_out->list_lock, flags);
	list_add_tail(&new_ele->head, &rfnm_usb_req_buffer_out->active);
	spin_unlock_irqrestore(&rfnm_usb_req_buffer_out->list_lock, flags);

	wake_up(&wq_usb);	// the ingest thread waits on this queue

	//wake_up(&wq_out);
	//tasklet_schedule(&rfnm_tasklet_out);

	// actual was working before... what changed?
	rfnm_stream_stats.usb_tx_bytes[0] += req->actual;
#else

	// actual was working before... what changed?
	rfnm_stream_stats.usb_tx_bytes[0] += req->actual;

	status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		printk("kill %s:  resubmit %d bytes --> %d\n", ep->name, req->length, status);
		rfnm_usb_ep_halt_guarded(ep, status);
		// FIXME recover later ... somehow 
	}

#endif
	






	/*status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		printk("kill %s:  resubmit %d bytes --> %d\n", ep->name, req->length, status);
		rfnm_usb_ep_halt_guarded(ep, status);
		// FIXME recover later ... somehow 
	}*/
}

EXPORT_SYMBOL_GPL(rfnm_submit_usb_req_out);

/*
{
	struct iio_rfnm_buffer *iio_rfnm_buffer = iio_buffer_to_rfnm_buffer(&queue->buffer);

	*gpio4 = *gpio4 | (0x1 << 1); *gpio4 = *gpio4 & ~(0x1 << 1);

	spin_lock_irq(&iio_rfnm_buffer->queue.list_lock);
	list_add_tail(&block->head, &iio_rfnm_buffer->active);
	spin_unlock_irq(&iio_rfnm_buffer->queue.list_lock);

	tasklet_schedule(&rfnm_tasklet);

	return 0;
}
*/


void rfnm_populate_dev_status(struct rfnm_dev_status * r_stat) {
	int i;

	memcpy(&r_stat->stream_stats, &rfnm_stream_stats, sizeof(struct rfnm_stream_stats));

	r_stat->tx_ring_read_ptr = rfnm_la9310_status->tx_buf_id;
	r_stat->tx_state = rfnm_la9310_status->tx_state;
	r_stat->tx_stream_seq = rfnm_la9310_status->tx_stream_seq;
	r_stat->tx_ring_head = rfnm_dev->tx_la_cb.head;

	// phytimer phase 1: pass the fw-published RX timing anchor through (the stream ack)
	r_stat->rx_t0 = rfnm_la9310_status->rx_t0;
	r_stat->rx_epoch = rfnm_la9310_status->rx_epoch;
	r_stat->rx_r_shift = rfnm_la9310_status->rx_r_shift;
	r_stat->rx_regate_cnt = rfnm_la9310_status->rx_regate_cnt;
	r_stat->tx_t0 = rfnm_la9310_status->tx_t0;
	r_stat->tx_epoch = rfnm_la9310_status->tx_epoch;
	r_stat->tx_r_shift = rfnm_la9310_status->tx_r_shift;
	r_stat->tx_underrun_cnt = rfnm_la9310_status->tx_underrun_cnt;
	r_stat->tx_timed_reject_cnt = rfnm_tx_stat_timed_reject;

	// current tick via the C21 spare capture; a concurrent trigger from the timed-tx
	// pace check races harmlessly (both read a capture taken within the last few us)
	r_stat->phytimer_now = rfnm_ptmr_now_cb ? rfnm_ptmr_now_cb() : 0;

	//memcpy(&r_stat->m7_status, (uint8_t *) rfnm_m7_status, sizeof(struct rfnm_m7_status));	
	// kazan freezes during this memcpy -- just copy it over manually

////	r_stat->m7_status.tx_buf_id = rfnm_m7_status->tx_buf_id;
//	r_stat->m7_status.tx_buf_id = rfnm_la9310_status->tx_buf_id;
////	r_stat->m7_status.rx_head = rfnm_m7_status->rx_head;
//	r_stat->m7_status.rx_head =  rfnm_la9310_status->rx_buf_id;
////	r_stat->m7_status.kernel_cache_flush_tail = rfnm_m7_status->kernel_cache_flush_tail;
	
	// cc is advanced by an extra element from the main loop
	if(rfnm_dev->tx_la_cb.usb_cc > 0) {
		r_stat->usb_dac_last_dqbuf[0] = rfnm_dev->tx_la_cb.usb_cc - 1;
	} else {
		r_stat->usb_dac_last_dqbuf[0] = 0;
	}

	//printk("r_stat->usb_dac_last_dqbuf[0] is %ld\n", r_stat->usb_dac_last_dqbuf[0]);

	
	for(i = 0; i < 4; i++) {
		r_stat->usb_adc_last_qbuf[i] = rfnm_dev->rx_usb_cb.usb_cc[i];
		r_stat->local_adc_last_qbuf[i] = rfnm_dev->rx_local_cb.local_cc[i];
	}
	



	//printk("%d\n", r_stat->usb_dac_last_dqbuf);

}
EXPORT_SYMBOL(rfnm_populate_dev_status);

// exactness plan: the feed contract - publish what the pump actually enforces so
// clients size their lead from the device instead of folklore constants
void rfnm_populate_dev_status_ext(struct rfnm_dev_status_ext *r) {
	BUILD_BUG_ON(sizeof(struct rfnm_dev_status_ext) > RFNM_SYSCTL_TRANSFER_SIZE);
	r->phytimer_now64 = rfnm_phytimer_now64();	// FRESH at request time, never cached
	r->phy_gen = READ_ONCE(rfnm_phy_gen);
	r->phy_rsvd = 0;
	uint32_t ticks_per_slot = 128u << rfnm_la9310_status->tx_r_shift;
	uint32_t staleness_slots = rfnm_tx_slot_rate_hz ? (uint32_t)div_u64(rfnm_tx_slot_rate_hz, 1000) : RFNM_TX_LEAD_SLOTS;

	rfnm_populate_dev_status(&r->base);
	r->tx_feed_lead_ticks = (RFNM_TX_LEAD_SLOTS + staleness_slots) * ticks_per_slot;
	r->rx_flush_deadline_us = (uint32_t)rx_flush_us;
	// Pump-event telemetry (both cumulative since insmod, never reset)
	r->tx_pace_rolls = rfnm_tx_stat_pace_rolls;
	r->tx_arm_repairs = rfnm_tx_stat_arm_repairs;
	// v3 bundle: POS placement outcomes, client-visible at last (cumulative; the
	// same counters the module params expose - diff across a session)
	r->tx_pos_placed = rfnm_tx_stat_pos_placed;
	r->tx_pos_late = rfnm_tx_stat_pos_late;
	r->tx_pos_stale = rfnm_tx_stat_pos_stale;
	r->tx_pos_misaligned = rfnm_tx_stat_pos_misaligned;
	// honest usable minimum: the 16-slot placement guard + one publish/staleness
	// allowance (1 ms of slots) - r6's measured 1.0-1.2 ms floor in tick form
	r->tx_feed_min_lead_ticks = rfnm_tx_min_lead_ticks(ticks_per_slot);	// = THE judge's constant
	r->sched_anchor_step_ticks = rfnm_sched_anchor_step_ticks;
	// reserved until their producers land (DMEM aperture proof)
	r->tx_underrun_axiq = 0;
	r->tx_underrun_ddr_rd = 0;
	// r7: the slip-aware tail's live estimate (ring slots -> ticks); ~0 for healthy
	// sparse shapes, grows with slip accumulation, informs client drift compensation
	r->tx_slip_ticks = 0;	// tombstone: the slip estimator died with the tail zoo
	// v4: burst attribution for the pos-late/stale counters above
	r->tx_last_late_usb_cc = rfnm_tx_last_late_usb_cc;
}
EXPORT_SYMBOL(rfnm_populate_dev_status_ext);




static ssize_t dfs_rfnm_stream_status_read(struct file *f, char *buffer, size_t len, loff_t *offset)
{
	char data[2048];	// rx18 block added ~600 B; 1000 was already near-full
	int data_len = 0;

	uint64_t time_diff, time_processing_start;
	static uint64_t last_print_time = 0;
	static struct rfnm_stream_stats last_stats;

	
	time_processing_start = ktime_get();
	time_diff = time_processing_start - last_print_time;
	last_print_time = time_processing_start;

	


	data_len += sprintf(&data[data_len], "usb rx ok:\t\t%ld\t%ld\n", rfnm_stream_stats.usb_rx_ok[0], rfnm_stream_stats.usb_rx_ok[1]);
	data_len += sprintf(&data[data_len], "usb rx error:\t%ld\t%ld\n", rfnm_stream_stats.usb_rx_error[0], rfnm_stream_stats.usb_rx_error[1]);
	// The local ship/consume loss witnesses were counted but never printed —
	// [0] = ship-time descriptor-pool NULL (packet+cc burned), [1] = zero-copy
	// slot lap retirements at GET (producer overwrote a checked-out/queued slot)
	data_len += sprintf(&data[data_len], "local rx ok:\t%ld\tpool_null:\t%ld\tlap_retire:\t%ld\tcanary:\t%llu\topen_refused:\t%llu\n",
		rfnm_stream_stats.local_rx_ok[0], rfnm_stream_stats.local_rx_error[0], rfnm_stream_stats.local_rx_error[1],
		(unsigned long long)rfnm67_canary, (unsigned long long)rfnm67_open_refused);
	data_len += sprintf(&data[data_len], "usb rx partial:\t%lld\n", rfnm_rx_partial_flush_cnt);
	data_len += sprintf(&data[data_len], "wrk passes:\t%lld slots:\t%lld subs:\t%lld break0:\t%lld\n",
		rfnm_dbg_passes, rfnm_dbg_slots, rfnm_dbg_subs, rfnm_dbg_break0);

	{
		// staging census: every place an RX buffer can hide between the VSPA ring and
		// the transports - reading this at wedge time locates the leak in one run
		struct usb_ep_queue_ele *ce;
		struct rfnm_local_buffer_queue_ele *cle;
		int free_in = 0, staged_in = 0, local_act = 0, local_free = 0;

		spin_lock(&rfnm_usb_req_buffer_in->list_lock);
		list_for_each_entry(ce, &rfnm_usb_req_buffer_in->active, head) { free_in++; }
		spin_unlock(&rfnm_usb_req_buffer_in->list_lock);
		spin_lock(&rfnm_usb_req_buffer_in_usb->list_lock);
		list_for_each_entry(ce, &rfnm_usb_req_buffer_in_usb->active, head) { staged_in++; }
		spin_unlock(&rfnm_usb_req_buffer_in_usb->list_lock);
		spin_lock(&rfnm_local_buffer_in->list_lock);
		list_for_each_entry(cle, &rfnm_local_buffer_in->active, head) { local_act++; }
		spin_unlock(&rfnm_local_buffer_in->list_lock);
		spin_lock(&rfnm_local_buffer_in_user->list_lock);
		list_for_each_entry(cle, &rfnm_local_buffer_in_user->active, head) { local_free++; }
		spin_unlock(&rfnm_local_buffer_in_user->list_lock);
		data_len += sprintf(&data[data_len],
			"rx staging: free %d staged %d inflight %d | local act %d free %d | usb head %d local head %d\n",
			free_in, staged_in, atomic_read(&rfnm_usb_inflight_cnt), local_act, local_free,
			rfnm_dev->rx_usb_cb.head, rfnm_dev->rx_local_cb.head);
		// pool population arithmetic: births - deaths must equal the census above; a
		// growing shortfall = elements lost OUTSIDE the known destroy paths
		data_len += sprintf(&data[data_len],
			"rx18 pool: created %u destroyed %u accounted %d (short %d) topup %u rate_sweeps %u\n",
			rfnm_rx18.req_created, rfnm_rx18.req_destroyed,
			free_in + staged_in + atomic_read(&rfnm_usb_inflight_cnt),
			(int)(rfnm_rx18.req_created - rfnm_rx18.req_destroyed)
				- (free_in + staged_in + atomic_read(&rfnm_usb_inflight_cnt)),
			rfnm_rx18.pool_topup, rfnm_rx18.rate_sweeps);
	}

	{
		// One-glance RX verdict block. fw ring pace: rx_buf_id sampled 2 ms
		// apart names "fw stopped producing" vs "producer stopped consuming" without
		// dmesg archaeology; the gate states say WHY staging refused.
		unsigned long last_submit = READ_ONCE(rfnm_usb_last_submit_jiffies);
		unsigned long grace_until = READ_ONCE(rfnm_usb_head_grace_until);
		uint32_t rh1, rh2, rt, ag1, ag2;

		rh1 = rfnm_la9310_status->rx_buf_id;
		ag1 = rfnm_la9310_status->age;
		usleep_range(1900, 2100);
		rh2 = rfnm_la9310_status->rx_buf_id;
		ag2 = rfnm_la9310_status->age;
		rt = rfnm_dev->rx_la_cb.tail;
		// age_2ms splits a frozen head: age advancing + head frozen = the VSPA main
		// loop lives but its RX service produces nothing (lane park); age frozen =
		// the whole status publish is dead (VSPA loop / status DMA channel)
		data_len += sprintf(&data[data_len],
			"rx18 fw ring: head %u tail %u pace_2ms %u slots age_2ms %u\n",
			rh2, rt, (rh2 - rh1) % RFNM_ADC_BUFCNT, ag2 - ag1);
		data_len += sprintf(&data[data_len],
			"rx18 gates: consumer_alive %d (last status-0 %d ms ago) grace %d ctrl_alive %d local_alive %d stage_gated %d standdown %d\n",
			rfnm_usb_rx_consumer_alive(),
			last_submit ? jiffies_to_msecs(jiffies - last_submit) : -1,
			rfnm_usb_rx_head_grace(), rfnm_host_ctrl_alive(), rfnm_local_rx_consumer_alive(),
			rfnm_usb_rx_stage_gated(), rfnm_rx_standdown_active);
		data_len += sprintf(&data[data_len],
			"rx18 prod: passes %llu ring_empty %llu standdown %llu/%u\n",
			rfnm_rx18.prod_passes, rfnm_rx18.ring_empty,
			rfnm_rx18.standdown_passes, rfnm_rx18.standdown_entries);
		data_len += sprintf(&data[data_len],
			"rx18 heal: rx_epoch %u regate_cnt %u sched %u ran %u\n",
			rfnm_la9310_status->rx_epoch, rfnm_la9310_status->rx_regate_cnt,
			rfnm_rx18.regate_sched, rfnm_rx18.regate_ran);
		data_len += sprintf(&data[data_len],
			"rx18 ship: full ok %llu gated %llu pool_empty %llu | partial ok %llu gated %llu pool_empty %llu | udc_pokes %u queue_fail %u\n",
			rfnm_rx18.full_ok, rfnm_rx18.full_gated, rfnm_rx18.full_pool_empty,
			rfnm_rx18.partial_ok, rfnm_rx18.partial_gated, rfnm_rx18.partial_pool_empty,
			rfnm_rx18.udc_pokes, rfnm_rx18.queue_fail);
		data_len += sprintf(&data[data_len],
			"rx18 local: deadship %llu\n", rfnm_rx18.local_deadship);
	}

	data_len += sprintf(&data[data_len], "\n");

	data_len += sprintf(&data[data_len], "usb tx ok:\t\t%ld\t%ld\n", rfnm_stream_stats.usb_tx_ok[0], rfnm_stream_stats.usb_tx_ok[1]);
	data_len += sprintf(&data[data_len], "usb tx error:\t%ld\t%ld\n", rfnm_stream_stats.usb_tx_error[0], rfnm_stream_stats.usb_tx_error[1]);

	data_len += sprintf(&data[data_len], "\n");

	


	uint64_t usb_rx_data_diff = rfnm_stream_stats.usb_rx_bytes[0] - last_stats.usb_rx_bytes[0];
	uint64_t usb_rx_data_rate = ((usb_rx_data_diff / 1000) / (time_diff / (1000 * 1000))) / (1);


	uint64_t usb_tx_data_diff = rfnm_stream_stats.usb_tx_bytes[0] - last_stats.usb_tx_bytes[0];
	uint64_t usb_tx_data_rate = ((usb_tx_data_diff / 1000) / (time_diff / (1000 * 1000))) / (1);


	data_len += sprintf(&data[data_len], "usb tx bw:\t%lld (MB/s)\n", usb_tx_data_rate);
	data_len += sprintf(&data[data_len], "usb rx bw:\t%lld (MB/s)\n", usb_rx_data_rate);

	data_len += sprintf(&data[data_len], "\n");


	data_len += sprintf(&data[data_len], "adc ok:\t\t%ld\t%ld\t%ld\t%ld\n", rfnm_stream_stats.la_adc_ok[0], rfnm_stream_stats.la_adc_ok[1], 
		rfnm_stream_stats.la_adc_ok[2], rfnm_stream_stats.la_adc_ok[3]);
	data_len += sprintf(&data[data_len], "adc error:\t%ld\t%ld\t%ld\t%ld\n", rfnm_stream_stats.la_adc_error[0], rfnm_stream_stats.la_adc_error[1],
		rfnm_stream_stats.la_adc_error[2], rfnm_stream_stats.la_adc_error[3]);
	data_len += sprintf(&data[data_len], "phytimer error:\t%ld\t%ld\t%ld\t%ld\n", rfnm_stream_stats.la_phytimer_error[0], rfnm_stream_stats.la_phytimer_error[1],
		rfnm_stream_stats.la_phytimer_error[2], rfnm_stream_stats.la_phytimer_error[3]);
	data_len += sprintf(&data[data_len], "rx seam: ship_chain %llu %llu %llu %llu inflight_ovw %llu ships %llu\n",
		(unsigned long long) rfnm_rx_seam.ship_chain_err[0], (unsigned long long) rfnm_rx_seam.ship_chain_err[1],
		(unsigned long long) rfnm_rx_seam.ship_chain_err[2], (unsigned long long) rfnm_rx_seam.ship_chain_err[3],
		(unsigned long long) rfnm_rx_seam.inflight_overwrite, (unsigned long long) rfnm_rx_seam.ship_log_wr);
	{
		int sk;
		uint64_t swr = rfnm_rx_seam.ship_log_wr;
		data_len += sprintf(&data[data_len], "rx ship log (last 8, newest first):\n");
		for (sk = 1; sk <= 8 && (uint64_t) sk <= swr; sk++) {
			struct rfnm_rx_ship_rec *sr = &rfnm_rx_ship_log[(swr - sk) % RFNM_RX_SHIP_LOG_N];
			data_len += sprintf(&data[data_len], "  cc %llu pt %u elems %u slot %u lane %u path %u\n",
				(unsigned long long) sr->cc, sr->pt, sr->elems, sr->slot, sr->lane, sr->path);
		}
	}

	data_len += sprintf(&data[data_len], "\n");

	data_len += sprintf(&data[data_len], "dac ok:\t\t%ld\n", rfnm_stream_stats.la_dac_ok[0]);
	data_len += sprintf(&data[data_len], "dac error:\t%ld\n", rfnm_stream_stats.la_dac_error[0]);
	// Forensics: the fw's RX stamp-chain debug tail (status words 16..20; read
	// raw past the legacy 64 B - no mirror-struct/vermagic exposure). Zeros on a
	// pre-debug eld (the fw DMA'd only 64 B there).
	if(rfnm_la9310_status) {
		volatile uint32_t *dbg = (volatile uint32_t *)rfnm_la9310_status + 16;
		data_len += sprintf(&data[data_len],
				"rx66: arm_t0 %u arms %u chunks %u arms50 %u arms200 %u arm2_t0 %u arm2_chunks %u arms10 %u\n",
				dbg[0], dbg[1], dbg[2], dbg[3], dbg[4], dbg[5], dbg[6], dbg[7]);
	}






	data_len += sprintf(&data[data_len], "\n");

	data_len += sprintf(&data[data_len], "\t\thead\ttail\treadable\t\n");



	uint32_t la_tail = rfnm_la9310_status->tx_buf_id;
	uint32_t la_head;

	// There is no TX write head - every packet places by its stamp.
	data_len += sprintf(&data[data_len], "writer:\t\t-\t%d\t- (one lane, stamp-placed)\n", la_tail);


	//la_head = rfnm_m7_status->rx_head;
	la_head = rfnm_la9310_status->rx_buf_id;
	la_tail = rfnm_dev->rx_la_cb.tail;

	uint32_t la_readable = la_head - la_tail;

	if(la_head < la_tail) {
		la_readable += RFNM_ADC_BUFCNT;
	}

	data_len += sprintf(&data[data_len], "reader:\t\t%d\t%d\t%d\n", la_head, la_tail, la_readable);

	data_len += sprintf(&data[data_len], "\n");


	uint32_t ls_in, ls_in_usb, ls_out, ls_out_usb;
	uint32_t ls_in_local, ls_in_local_user, ls_out_local, ls_out_local_user;

	spin_lock(&rfnm_usb_req_buffer_in->list_lock);
	ls_in = list_count_nodes(&rfnm_usb_req_buffer_in->active);
	spin_unlock(&rfnm_usb_req_buffer_in->list_lock);

	spin_lock(&rfnm_usb_req_buffer_in_usb->list_lock);
	ls_in_usb = list_count_nodes(&rfnm_usb_req_buffer_in_usb->active);
	spin_unlock(&rfnm_usb_req_buffer_in_usb->list_lock);

	spin_lock(&rfnm_usb_req_buffer_out->list_lock);
	ls_out = list_count_nodes(&rfnm_usb_req_buffer_out->active);
	spin_unlock(&rfnm_usb_req_buffer_out->list_lock);

	spin_lock(&rfnm_usb_req_buffer_out_usb->list_lock);
	ls_out_usb = list_count_nodes(&rfnm_usb_req_buffer_out_usb->active);
	spin_unlock(&rfnm_usb_req_buffer_out_usb->list_lock);


	spin_lock(&rfnm_local_buffer_in->list_lock);
	ls_in_local = list_count_nodes(&rfnm_local_buffer_in->active);
	spin_unlock(&rfnm_local_buffer_in->list_lock);

	spin_lock(&rfnm_local_buffer_in_user->list_lock);
	ls_in_local_user = list_count_nodes(&rfnm_local_buffer_in_user->active);
	spin_unlock(&rfnm_local_buffer_in_user->list_lock);

	spin_lock(&rfnm_local_buffer_out->list_lock);
	ls_out_local = list_count_nodes(&rfnm_local_buffer_out->active);
	spin_unlock(&rfnm_local_buffer_out->list_lock);

	spin_lock(&rfnm_local_buffer_out_user->list_lock);
	ls_out_local_user = list_count_nodes(&rfnm_local_buffer_out_user->active);
	spin_unlock(&rfnm_local_buffer_out_user->list_lock);
	
	

	data_len += sprintf(&data[data_len], "list size\t\tusb\t\tlocal\n", ls_in);

	data_len += sprintf(&data[data_len], "in:\t\t\t%d\t\t%d\n", ls_in, ls_in_local);
	data_len += sprintf(&data[data_len], "in user:\t\t%d\t\t%d\n", ls_in_usb, ls_in_local_user);
	data_len += sprintf(&data[data_len], "out:\t\t\t%d\t\t%d\n", ls_out, ls_out_local);
	data_len += sprintf(&data[data_len], "out user:\t\t%d\t\t%d\n", ls_out_usb, ls_out_local_user);

	


	memcpy(&last_stats, &rfnm_stream_stats, sizeof(struct rfnm_stream_stats));

	return simple_read_from_buffer(buffer, len, offset, data, data_len);
}
/*
static inline struct task_struct *
kthread_run_on_cpu(int (*threadfn)(void *data), void *data,
			unsigned int cpu, const char *namefmt)
{
	struct task_struct *p;

	p = kthread_create_on_cpu(threadfn, data, cpu, namefmt);
	if (!IS_ERR(p))
		wake_up_process(p);

	return p;
}
*/

// TX health: one debugfs read = the whole arm/schedule story - fw pump state + pace
// sample, walker request-ring counters (executed/missed/rejected were host-readable
// since v3 phase 1 but nothing ever read them), and the docs-5q AXIQ/gate registers.
// Closes the fire-and-forget telemetry hole: cat this after any timed run.
static ssize_t dfs_rfnm_tx_health_read(struct file *f, char *buffer, size_t len, loff_t *offset)
{
	char data[800];
	int data_len = 0;
	struct rfnm_tx_health h;
	uint32_t tl1, tl2, sr1_tx;

	memset(&h, 0xff, sizeof(h));
	if(rfnm_tx_health_cb) {
		rfnm_tx_health_cb(&h);
	}
	tl1 = rfnm_la9310_status->tx_buf_id;
	usleep_range(1900, 2100);
	tl2 = rfnm_la9310_status->tx_buf_id;
	sr1_tx = (h.axiq_sr1 >> 16) & 0xf;

	data_len += scnprintf(data + data_len, sizeof(data) - data_len,
			"tx_state 0x%x epoch %u t0 %u tail %u pace_2ms %u slots\n",
			rfnm_la9310_status->tx_state, rfnm_la9310_status->tx_epoch,
			rfnm_la9310_status->tx_t0, tl2, (tl2 - tl1) % RFNM_DAC_BUFCNT);
	// Phase witness: the one-lane placement law is slot = ((stamp - t0)
	// / tps) & mask, which is TRUE iff the fw's read cursor obeys tail == ((now -
	// t0) / tps) & mask. phase_delta = tail - model in ring space (signed-folded):
	// 0 = the law holds; anything else = placement lands that many slots away from
	// where the fw actually reads (the dark-TX failure class).
	{
		uint64_t now64 = rfnm_phytimer_now64();
		uint32_t tps_h = 128u << rfnm_la9310_status->tx_r_shift;
		uint32_t model = (((uint32_t)now64 - rfnm_la9310_status->tx_t0) / tps_h) & (RFNM_DAC_BUFCNT - 1);
		int32_t pd = (int32_t)((tl2 - model) & (RFNM_DAC_BUFCNT - 1));

		if(pd > RFNM_DAC_BUFCNT / 2) {
			pd -= RFNM_DAC_BUFCNT;
		}
		data_len += scnprintf(data + data_len, sizeof(data) - data_len,
				"now64 %llu model_slot %u phase_delta %d slots\n",
				(unsigned long long)now64, model, pd);
	}
	// ring-peek witness (dark-TX debugging): the same slots through BOTH views.
	// "cache" = the driver's WB mapping after inval (A53 upgrades IVAC on dirty
	// lines to clean+inval, so this view can self-heal what it observes). "ddr" =
	// an uncached Device alias = the bytes the VSPA's PCIe fetch actually gets.
	{
		static void __iomem *dac_uc;
		uint32_t pk[2] = { (tl2 + 256) & (RFNM_DAC_BUFCNT - 1), (tl2 - 256) & (RFNM_DAC_BUFCNT - 1) };
		int i;

		if(!dac_uc) {
			dac_uc = ioremap(RFNM_IQFLOOD_DAC_MEMADDR, SZ_64M);
		}
		for(i = 0; i < 2; i++) {
			uint32_t *w = (uint32_t *)rfnm_bufdesc_tx[pk[i]].buf;
			uint32_t u[4] = { 0, 0, 0, 0 };

			if(dac_uc) {
				memcpy_fromio(u, dac_uc + (size_t)pk[i] * sizeof(struct rfnm_bufdesc_tx), 16);
			}
			dcache_inval_poc(RFNM_CACHE_ADDR(&rfnm_bufdesc_tx[pk[i]]), RFNM_CACHE_ADDR(&rfnm_bufdesc_tx[pk[i] + 1]));
			data_len += scnprintf(data + data_len, sizeof(data) - data_len,
					"peek[%s %u] ddr %08x %08x %08x %08x cache %08x %08x %08x %08x\n",
					i ? "behind" : "ahead", pk[i], u[0], u[1], u[2], u[3],
					w[0], w[1], w[2], w[3]);
		}
	}
	data_len += scnprintf(data + data_len, sizeof(data) - data_len,
			"pace_ok %d rolls %u (one lane: no modes, no tail estimates)\n",
			rfnm_tx_pace_ok, rfnm_tx_stat_pace_rolls);
	data_len += scnprintf(data + data_len, sizeof(data) - data_len,
			"timed_placed %u timed_reject %u mode_reject %u unstamped %lu\n",
			rfnm_tx_stat_timed_placed, rfnm_tx_stat_timed_reject, rfnm_tx_stat_mode_reject,
			rfnm_tx_stat_unstamped);
	data_len += scnprintf(data + data_len, sizeof(data) - data_len,
			"walker prod %u cons %u executed %u missed %u rejected %u qs_stuck %u\n",
			h.txn_prod, h.txn_cons, h.txn_executed, h.txn_missed, h.txn_rejected, h.txn_qs_stuck);
	data_len += scnprintf(data + data_len, sizeof(data) - data_len,
			"axiq sr1 0x%08x (tx0 %s%s%s%s) cr3 0x%08x (en %lu) c11_sc 0x%08x (line %s)\n",
			h.axiq_sr1,
			(sr1_tx & 0x1) ? "ENABLED" : "disabled", (sr1_tx & 0x2) ? "|NOTFULL" : "",
			(sr1_tx & 0x4) ? "|ERRUNDER" : "", (sr1_tx & 0x8) ? "|ERROVER" : "",
			h.axiq_cr3, h.axiq_cr3 & 0x1ul,
			h.c11_sc, (h.c11_sc & 0x80000000u) ? "OPEN" : "closed");

	return simple_read_from_buffer(buffer, len, offset, data, data_len);
}

const struct file_operations dfs_rfnm_tx_health_fops = {
	.owner = THIS_MODULE,
	.read = dfs_rfnm_tx_health_read,
};

// v8 margin witness reader: the fw's l1_trace crash-cam (96 x {ccnt,msg,param}
// u32 triplets) lands at RFNM_IQFLOOD_L1TRACE_MEMADDR - a no-map carveout, so
// devmem lies there and this node is the kernel-side truth. Raw hex, one
// record per line, ring order as stored; a decoder can find the PARK record
// (msg 0x40f) = the newest entry.
static ssize_t dfs_rfnm_l1trace_read(struct file *f, char *buffer, size_t len, loff_t *offset)
{
	static void __iomem *l1t_map;
	char data[96 * 28 + 32];
	uint32_t rec[3];
	int data_len = 0, i;

	if(!l1t_map) {
		l1t_map = ioremap(RFNM_IQFLOOD_L1TRACE_MEMADDR, SZ_4K);
		if(!l1t_map) {
			return -ENOMEM;
		}
	}
	for(i = 0; i < 96; i++) {
		memcpy_fromio(rec, l1t_map + i * 12, 12);
		data_len += scnprintf(data + data_len, sizeof(data) - data_len,
				"%08x %08x %08x\n", rec[0], rec[1], rec[2]);
	}
	return simple_read_from_buffer(buffer, len, offset, data, data_len);
}

const struct file_operations dfs_rfnm_l1trace_fops = {
	.owner = THIS_MODULE,
	.read = dfs_rfnm_l1trace_read,
};

const struct file_operations dfs_rfnm_stream_fops = {
	.owner = THIS_MODULE,
	.read = dfs_rfnm_stream_status_read,
};


static struct dentry *dfs_rfnm_dir;
static struct dentry *dfs_rfnm_stream_stat;

int stop_sm(void) {
	unsigned long timeout = jiffies + msecs_to_jiffies(1500);

	if(atomic_read(&rfnm_sm_workers_alive) == 0) {
		return 0;
	}

	rfnm_dev->wq_stop_in = 1;
	rfnm_dev->wq_stop_out = 1;
	rfnm_dev->wq_stop_usb = 1;
	wake_up(&wq_usb);

	while(atomic_read(&rfnm_sm_workers_alive)) {
		if(time_after(jiffies, timeout)) {
			pr_err("RFNM: timed out stopping stream workers (%d alive, stop flags %d/%d/%d)\n",
				atomic_read(&rfnm_sm_workers_alive), rfnm_dev->wq_stop_in, rfnm_dev->wq_stop_out, rfnm_dev->wq_stop_usb);
			return -ETIMEDOUT;
		}
		msleep(1);
	}

	rfnm_dev->wq_stop_in = 0;
	rfnm_dev->wq_stop_out = 0;
	rfnm_dev->wq_stop_usb = 0;

	return 0;
}

/*
void start_sm(void) {
	
	kthread_run_on_cpu(rfnm_handler_in, NULL, 1, "RX");
	kthread_run_on_cpu(rfnm_handler_out, NULL, 2, "TX");
	kthread_run_on_cpu(rfnm_handler_usb, NULL, 3, "USB");
}*/

static void start_sm_work(struct work_struct *work) {
	struct task_struct *t;
	int (*fns[3])(void *) = { rfnm_handler_in, rfnm_handler_out, rfnm_handler_usb };
	static const char *names[3] = { "RX", "TX", "USB" };
	int i;

	if(atomic_read(&rfnm_sm_workers_alive) != 0) {
		pr_err("RFNM: not starting stream workers, %d still alive\n", atomic_read(&rfnm_sm_workers_alive));
		return;
	}

	for(i = 0; i < 3; i++) {
		atomic_inc(&rfnm_sm_workers_alive);
		t = kthread_run_on_cpu(fns[i], NULL, i + 1, names[i]);
		if(IS_ERR(t)) {
			atomic_dec(&rfnm_sm_workers_alive);
			pr_err("RFNM: failed to start %s stream worker: %ld\n", names[i], PTR_ERR(t));
		}
	}
}
static DECLARE_WORK(start_sm_wq, start_sm_work);
static DEFINE_MUTEX(rfnm_hard_reset_lock);
static atomic_t rfnm_sm_restart_running = ATOMIC_INIT(0);
static atomic_t rfnm_hard_reset_running = ATOMIC_INIT(0);
static int rfnm_hard_reset_status = RFNM_API_OK;
static uint64_t rfnm_hard_reset_dcs_freq = 122880000;


//int tcp_can_work = 0;
//EXPORT_SYMBOL(tcp_can_work);
static void (*rfnm_dgb_reset_cb)(void);
static void (*rfnm_lalib_quiesce_cb)(void);

static void rfnm_reset_stream_io_state(void) {

	int i;

//	tcp_can_work = 0; // ugly I know;

	/* flush stale in-flight RX requests: a new session must never start behind queued bytes
	 * from the old one (the drain time scales inversely with sample rate and starves librfnm
	 * below ~15M). Dequeued requests complete with -ECONNRESET and recycle into the pool. */
	for (i = 0; rfnm_usb_req_inflight_in && i < 256; i++) {
		struct usb_ep_queue_ele *fl;
		unsigned long fl_flags;

		spin_lock_irqsave(&rfnm_usb_req_inflight_in->list_lock, fl_flags);
		fl = list_first_entry_or_null(&rfnm_usb_req_inflight_in->active, struct usb_ep_queue_ele, head);
		spin_unlock_irqrestore(&rfnm_usb_req_inflight_in->list_lock, fl_flags);
		if (!fl) {
			break;
		}
		usb_ep_dequeue(fl->ep, fl->req);
		usleep_range(100, 200);			// completion may be deferred; bounded wait
	}

	/* stale ASSEMBLED packets (staged for shipping, never queued to the wire) must not
	 * ship into the new session either - they carried the old session's ccs/stamps in
	 * dup/reordered bursts at the next stream's head. Recycle them into the request
	 * pool unsent (workers are stopped here, but take the locks anyway). */
	while (rfnm_usb_req_buffer_in_usb && rfnm_usb_req_buffer_in) {
		struct usb_ep_queue_ele *fl;
		unsigned long fl_flags;

		spin_lock_irqsave(&rfnm_usb_req_buffer_in_usb->list_lock, fl_flags);
		fl = list_first_entry_or_null(&rfnm_usb_req_buffer_in_usb->active, struct usb_ep_queue_ele, head);
		if (fl) {
			list_del(&fl->head);
		}
		spin_unlock_irqrestore(&rfnm_usb_req_buffer_in_usb->list_lock, fl_flags);
		if (!fl) {
			break;
		}
		spin_lock_irqsave(&rfnm_usb_req_buffer_in->list_lock, fl_flags);
		list_add_tail(&fl->head, &rfnm_usb_req_buffer_in->active);
		spin_unlock_irqrestore(&rfnm_usb_req_buffer_in->list_lock, fl_flags);
	}

	rfnm_dev->rx_usb_cb.head = 0;
	rfnm_dev->rx_usb_cb.cc = 0;
	rfnm_dev->rx_usb_cb.usb_host_dropped = 0;
	
	rfnm_dev->rx_local_cb.head = 0;
	rfnm_dev->rx_local_cb.cc = 0;
	rfnm_dev->rx_local_cb.local_host_dropped = 0;
	
	rfnm_dev->rx_la_cb.tail = 0;

	// stream (re)start is control-plane proof of a live client: stamp the ctrl recency
	// so the stood-down producer resumes before the session's first data, and re-arm
	// the per-stream chain diagnostics (print budget + first-fault slot dump)
	rfnm_note_host_ctrl();
	rfnm_rx_chain_print_cnt = 0;
	rfnm_rx_slotdump_done = 0;
	// cc chain re-anchors silently on the first sub of the new stream, same rule as the
	// phytimer chain below (the fw's sub cc counters are NOT reset by a stream command,
	// so anchoring at 0 burned one spurious la_adc_error per session)
	rfnm_rx_cc_anchor_pending = 0xf;

	for(i = 0; i < 4; i++) {
		rfnm_dev->rx_la_cb.adc_cc[i] = 0;
		// phytimer phase 1: chain re-anchors on the first sub of the new stream (the
		// stale-prefix drain re-anchors again via its epoch flip, without error)
		rfnm_dev->rx_la_cb.phytimer_valid[i] = 0;

		rfnm_dev->rx_usb_cb.adc_buf[i] = 0;
		//rfnm_dev->rx_usb_cb.adc_buf_cnt[i] = RFNM_RX_USB_BUF_MULTI;
		rfnm_dev->rx_usb_cb.adc_buf_size[i] = 0;
		/* usb_cc/local_cc deliberately NOT reset: they must stay monotonic across stream
		 * restarts. In-flight packets from the previous session drain to the host at the
		 * start of the next one; if the counter restarts at 0, librfnm inits its expected
		 * cc from the stale packets and then discards every new-session packet as ancient
		 * (total RX starvation, seen at low rates where the stale prefix lingers). With a
		 * monotonic counter the stale prefix is contiguous old data and flows out naturally. */

		rfnm_dev->rx_local_cb.adc_buf[i] = 0;
		//rfnm_dev->rx_local_cb.adc_buf_cnt[i] = RFNM_RX_USB_BUF_MULTI;
		rfnm_dev->rx_local_cb.adc_buf_size[i] = 0;
	}

	rfnm_dev->tx_la_cb.head = 0;
	rfnm_dev->tx_la_cb.dac_cc = 0;
	rfnm_dev->tx_la_cb.usb_cc = 0;

	rfnm_dev->wq_stop_in = 0;
	rfnm_dev->wq_stop_out = 0;
	rfnm_dev->wq_stop_usb = 0;






    

    /* Check that all our queue pointers are valid */
    if (!rfnm_usb_req_buffer_out || !rfnm_usb_req_buffer_out_usb ||
        !rfnm_local_buffer_out     || !rfnm_local_buffer_out_user  ||
        !rfnm_usb_req_buffer_in   || !rfnm_usb_req_buffer_in_usb   ||
        !rfnm_local_buffer_in     || !rfnm_local_buffer_in_user) {
        pr_err("flush_all_queues: One or more queue pointers are NULL!\n");
    } else {

		struct usb_ep_queue_ele *usb_elem, *usb_tmp;
	    struct rfnm_local_buffer_queue_ele *local_elem, *local_tmp;



		    /* Ensure that the list heads are properly initialized with INIT_LIST_HEAD() when the queues are created. */

			/* Flush the USB OUT queue */
			spin_lock(&rfnm_usb_req_buffer_out->list_lock);
			list_for_each_entry_safe(usb_elem, usb_tmp, &rfnm_usb_req_buffer_out->active, head) {
				spin_lock(&rfnm_usb_req_buffer_out_usb->list_lock);
				list_move_tail(&usb_elem->head, &rfnm_usb_req_buffer_out_usb->active);
				spin_unlock(&rfnm_usb_req_buffer_out_usb->list_lock);
			}
			spin_unlock(&rfnm_usb_req_buffer_out->list_lock);

			/* Flush the Local OUT queue */
			spin_lock(&rfnm_local_buffer_out->list_lock);
			list_for_each_entry_safe(local_elem, local_tmp, &rfnm_local_buffer_out->active, head) {
				spin_lock(&rfnm_local_buffer_out_user->list_lock);
				list_move_tail(&local_elem->head, &rfnm_local_buffer_out_user->active);
				spin_unlock(&rfnm_local_buffer_out_user->list_lock);
			}
			spin_unlock(&rfnm_local_buffer_out->list_lock);


			/* The USB IN queue is deliberately NOT flushed pool -> staged here. That
			 * legacy move (mirroring the OUT direction, where re-arming receive
			 * requests at reset is correct) re-staged EVERY pooled IN request with
			 * its stale length and stale ring pointer; the restarted USB worker then
			 * queued the lot to the wire, so session N>1 opened on a storm of the
			 * old session's packets and a pile of armed requests the new client
			 * never fully read - the poisoned-endpoint seed. It also silently undid
			 * the assembled-queue drain above. Pooled IN requests stay pooled; only
			 * the stager (with fresh, repointed data) or a host-driven set_alt may
			 * arm IN requests. */

			spin_lock(&rfnm_local_buffer_in_user->list_lock);
			list_for_each_entry_safe(local_elem, local_tmp, &rfnm_local_buffer_in_user->active, head) {
				spin_lock(&rfnm_local_buffer_in->list_lock);
				list_move_tail(&local_elem->head, &rfnm_local_buffer_in->active);
				spin_unlock(&rfnm_local_buffer_in->list_lock);
			}
			spin_unlock(&rfnm_local_buffer_in_user->list_lock);
	}








	

	/* NO device-side endpoint disable/enable here, deliberately. A device-only rearm at
	 * the session boundary resets the gadget's SuperSpeed per-endpoint sequence state
	 * while the host xHCI keeps its own - the pipes then desync and the new session's
	 * bulk transport runs dead (or crawls) until the host's SET_INTERFACE backstop
	 * resyncs BOTH sides (measured: schedule pushes arriving 8-40 ms apart, every
	 * window -ETIME; the failure ALTERNATED per session as the 5-bit seq delta happened
	 * to land aligned or not). The dequeue drain above is seq-preserving and, with the
	 * dwc3 watchdog hygiene fixes (kick guard, reclaim-once, cancel-on-disable), is
	 * sufficient reclaim; the full TRB/flag rebuild belongs exclusively to host-driven
	 * set_alt (enumeration and the librfnm dead-pipe resync), which is the only path
	 * that renews both sides of the link together. */
	rfnm_usb_inflight_gate_armed = 0;

	/* new-session head grace, armed at every stream-IO reset (the "new client incoming"
	 * barrier: session open, reclock hard reset) and AFTER the in-flight drain above, so
	 * a straggling completion of the old session cannot retire it before the new stream
	 * even starts. Cleared by the first status-0 IN completion or the deadline (see
	 * rfnm_usb_rx_head_grace). Resets driven by non-USB clients arm it too: the cost is
	 * one bounded full-depth arming if the USB side has no reader, and the next USB
	 * session's reset re-arms a fresh window either way. */
	WRITE_ONCE(rfnm_usb_head_grace_until, jiffies + msecs_to_jiffies(RFNM_USB_HEAD_GRACE_MS));

	memset(&rfnm_stream_stats, 0, sizeof(struct rfnm_stream_stats));
}

void rfnm_reset_sm(void) {
	if(rfnm_lalib_quiesce_cb) {
		rfnm_lalib_quiesce_cb();
	}

	if (rfnm_dgb_reset_cb) {
		rfnm_dgb_reset_cb();
	}

	rfnm_reset_stream_io_state();
}


void rfnm_submit_usb_req_in(struct usb_ep *ep, struct usb_request *req);

// Re-mint the IN pool deficit at every stream-IO reset. ESHUTDOWN-class
// deaths (stormy closes, teardown races) destroy pool elements permanently - the only
// alloc site was host-driven set_alt, which USB session opens never perform, so a few
// bad sessions ate the pool below the stall detector's reach (the terminal 0.55 Msps
// trickle). Fresh requests get placeholder bufs (the stager repoints into the carveout
// before arming) and enroll straight into the free pool; no endpoint state is touched
// (device-side rearm stays absent by design - SS seq-state desync). Process context.
static void rfnm_usb_in_pool_topup(void) {
	unsigned long flags;
	struct usb_ep_queue_ele *le;
	int alive, deficit, i, made = 0;

	if(!rfnm_usb_req_buffer_in || !rfnm_usb_in_ep_cnt) {
		return;
	}
	alive = atomic_read(&rfnm_usb_inflight_cnt);
	spin_lock_irqsave(&rfnm_usb_req_buffer_in->list_lock, flags);
	list_for_each_entry(le, &rfnm_usb_req_buffer_in->active, head) {
		alive++;
	}
	spin_unlock_irqrestore(&rfnm_usb_req_buffer_in->list_lock, flags);

	deficit = RFNM_USB_INFLIGHT_CAP - alive;
	for(i = 0; i < deficit; i++) {
		struct usb_ep *ep = rfnm_usb_in_eps[i % rfnm_usb_in_ep_cnt];
		struct usb_request *req = usb_ep_alloc_request(ep, GFP_KERNEL);
		struct usb_ep_queue_ele *ne;

		if(!req) {
			break;
		}
		req->buf = kmalloc(4096, GFP_KERNEL);
		if(!req->buf) {
			usb_ep_free_request(ep, req);
			break;
		}
		req->length = 4096;
		req->complete = rfnm_submit_usb_req_in;
		ne = kzalloc(sizeof(*ne), GFP_KERNEL);
		if(!ne) {
			rfnm_usb_req_free(ep, req);
			break;
		}
		ne->ep = ep;
		ne->req = req;
		spin_lock_irqsave(&rfnm_usb_req_buffer_in->list_lock, flags);
		list_add_tail(&ne->head, &rfnm_usb_req_buffer_in->active);
		spin_unlock_irqrestore(&rfnm_usb_req_buffer_in->list_lock, flags);
		made++;
	}
	if(made) {
		rfnm_rx18.req_created += made;
		rfnm_rx18.pool_topup += made;
		pr_info("RFNM: usb IN pool topped up +%d (alive was %d of %d)\n",
				made, alive, RFNM_USB_INFLIGHT_CAP);
	}
}

static int __rfnm_restart_sm(int hard) {

	if(hard) {
		if(stop_sm()) {
			pr_err("RFNM: sm restart aborted, stream workers did not stop\n");
			return -ETIMEDOUT;
		}
	} else {
		// bounded: this used to spin forever holding rfnm_hard_reset_lock when no
		// USB worker was around to clear flushmode (async spawn window / stuck
		// worker), wedging every subsequent device open until reboot
		unsigned long timeout = jiffies + msecs_to_jiffies(1000);

		rfnm_dev->usb_flushmode = 1;
		wake_up(&wq_usb);

		while(rfnm_dev->usb_flushmode) {
			if(atomic_read(&rfnm_sm_workers_alive) == 0 || time_after(jiffies, timeout)) {
				rfnm_dev->usb_flushmode = 0;
				pr_err("RFNM: sm flush skipped (%d workers alive)\n", atomic_read(&rfnm_sm_workers_alive));
				break;
			}
			msleep(1);
		}
	}

	// RF hygiene: the fw re-init restores the always-allowed gate baseline, so any
	// stale DAC-ring content (a dead remote session's staged windows - USB clients
	// have no close hook) would replay every ring lap until the next stream scrub.
	// Scrub NOW, while the workers are stopped and before the gates reopen.
	rfnm_tx_flush_staging(1);

	// Refill whatever the previous session's ESHUTDOWN storms ate
	rfnm_usb_in_pool_topup();

	rfnm_reset_sm();

	if(hard) {
		schedule_work(&start_sm_wq);
		flush_work(&start_sm_wq);
	}

	return 0;
}

int rfnm_restart_sm(int hard) {
	int ret;

	mutex_lock(&rfnm_hard_reset_lock);
	ret = __rfnm_restart_sm(hard);
	mutex_unlock(&rfnm_hard_reset_lock);

	// An SM restart is a session-ownership boundary - the incoming owner works
	// in the LIVE time generation (stale-generation refusals end with the old era)
	if(!ret) {
		WRITE_ONCE(rfnm_session_phy_gen, READ_ONCE(rfnm_phy_gen));
		WRITE_ONCE(rfnm_session_dead, 0);	// reopen is THE recovery
	}

	return ret;
}
EXPORT_SYMBOL(rfnm_restart_sm);

static void rfnm_restart_sm_work_fn(struct work_struct *work) {
	rfnm_restart_sm(1);
	atomic_set(&rfnm_sm_restart_running, 0);
}
static DECLARE_WORK(rfnm_restart_sm_work, rfnm_restart_sm_work_fn);

int rfnm_wait_restart_sm_idle(unsigned int timeout_ms) {
	unsigned long timeout = jiffies + msecs_to_jiffies(timeout_ms);

	while(atomic_read(&rfnm_sm_restart_running)) {
		if(time_after(jiffies, timeout)) {
			return -ETIMEDOUT;
		}
		msleep(1);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rfnm_wait_restart_sm_idle);

int rfnm_schedule_restart_sm(void) {
	if(atomic_cmpxchg(&rfnm_sm_restart_running, 0, 1) != 0) {
		return -EBUSY;
	}

	// The USB reset ack returns BEFORE the restart work runs, and the client's next verbs
	// race that work. The confirmed-apply protocol (GET_SET_RESULT cc match) is the
	// client's only ordering barrier - but the result block still holds the LAST session's
	// ccs, and a fresh client's first cc collides with them deterministically (every
	// session issues the same small cc sequence), so apply() false-confirms instantly and
	// the client's schedule config lands ahead of the restart's quiesce, which then
	// disarms it ~20 ms later (the windowed session-N>1 ungated-flood face). Invalidate
	// the stale ccs synchronously with the ack (memset-only callback, hardirq-safe): the
	// client then polls until the REAL apply work, which waits for restart-idle,
	// publishes fresh ccs on the far side of the reset.
	if(rfnm_dgb_reset_cb) {
		rfnm_dgb_reset_cb();
	}

	if(!schedule_work(&rfnm_restart_sm_work)) {
		atomic_set(&rfnm_sm_restart_running, 0);
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rfnm_schedule_restart_sm);

int rfnm_hard_reset_la9310(uint64_t dcs_freq) {
	int ret;

	if(!dcs_freq) {
		return -EINVAL;
	}

	flush_work(&rfnm_restart_sm_work);

	mutex_lock(&rfnm_hard_reset_lock);
	// The uncommanded-restart detector must not re-apply a stale word while a
	// commanded reset is mid-flight (the restarted timer reads backwards inside this
	// window on some rolls - observed 2-of-4 reclocks); the commanded path re-sends
	// its own word on the far side.
	atomic_set(&rfnm_commanded_reset_active, 1);

	pr_info("RFNM: stopping stream state machine before LA9310 hard reset to %llu Hz\n", (unsigned long long)dcs_freq);
	flush_work(&start_sm_wq);
	if(rfnm_lalib_quiesce_cb) {
		rfnm_lalib_quiesce_cb();
	}
	ret = stop_sm();
	if(ret) {
		pr_err("RFNM: aborting LA9310 hard reset because stream stop failed: %d\n", ret);
		goto out;
	}

	rfnm_reset_stream_io_state();

	ret = rfnm_la9310_hard_reprobe(dcs_freq);
	if (ret) {
		pr_err("RFNM: LA9310 hard reset failed: %d; stream state machine remains stopped\n", ret);
		goto out;
	}

	rfnm_reset_stream_io_state();
	// The chip reset restarted the phytimer - new time generation. The session
	// that COMMANDED this reset re-latches (its reclock re-bases its own promises);
	// every other holder of old-generation ticks gets TIME_RESET on its next verb.
	rfnm_phy_gen_bump("commanded LA9310 hard reset");
	WRITE_ONCE(rfnm_session_phy_gen, READ_ONCE(rfnm_phy_gen));
	schedule_work(&start_sm_wq);
	flush_work(&start_sm_wq);
	pr_info("RFNM: LA9310 hard reset complete; stream state machine restarted\n");

out:
	atomic_set(&rfnm_commanded_reset_active, 0);
	mutex_unlock(&rfnm_hard_reset_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(rfnm_hard_reset_la9310);

// Parked-core VSPA kernel swap on an UNCHANGED DCS - same SM brackets as the
// hard reset (quiesce word to the outgoing fw, workers stopped, IO state reset both
// sides) but no PCIe teardown, no chip reset, no phytimer restart (so no phy_gen bump
// and no TIME_RESET for other holders; the commanding apply re-sends its word on the
// fresh kernel). Serialized against hard resets by the same lock.
int rfnm_vspa_kernel_swap(uint32_t required_caps) {
	int ret;

	flush_work(&rfnm_restart_sm_work);
	mutex_lock(&rfnm_hard_reset_lock);
	pr_info("RFNM: vspa kernel swap for caps %02x: stopping stream state machine\n", required_caps);
	flush_work(&start_sm_wq);
	// NEVER quiesce an already-parked core. A parked (done) core
	// does not read its inbox, so the M4 forwards the redundant word and then waits
	// forever for a reply - and that pending read EATS the next kernel's Boot
	// Complete from the shared outbox (the whole lost-post mystery; the userspace
	// tool works because it never sends words). Quiesce only a BUSY core - which
	// replies before parking, leaving the M4 satisfied and the outbox clean.
	if(rfnm_vspa_core_busy() && rfnm_lalib_quiesce_cb) {
		rfnm_lalib_quiesce_cb();
		msleep(50);
	}
	ret = stop_sm();
	if(ret) {
		pr_err("RFNM: aborting vspa kernel swap because stream stop failed: %d\n", ret);
		goto out;
	}
	rfnm_reset_stream_io_state();
	if(rfnm_vspa_handoff_cb) {
		rfnm_vspa_handoff_cb(1);	// M4: hands off the VSPA mailboxes
	}
	ret = rfnm_vspa_select_for(required_caps);
	if(!ret) {
		ret = rfnm_vspa_boot_selected();
	}
	if(rfnm_vspa_handoff_cb) {
		rfnm_vspa_handoff_cb(0);	// handshake done (or failed) - M4 resumes
	}
	rfnm_reset_stream_io_state();
	schedule_work(&start_sm_wq);
	flush_work(&start_sm_wq);
	pr_info("RFNM: vspa kernel swap %s; stream state machine restarted\n", ret ? "FAILED" : "complete");
out:
	mutex_unlock(&rfnm_hard_reset_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(rfnm_vspa_kernel_swap);

static void rfnm_hard_reset_work_fn(struct work_struct *work) {
	int ret = rfnm_hard_reset_la9310(READ_ONCE(rfnm_hard_reset_dcs_freq));

	WRITE_ONCE(rfnm_hard_reset_status, ret ? RFNM_API_PROBE_FAIL : RFNM_API_OK);
	atomic_set(&rfnm_hard_reset_running, 0);
}
static DECLARE_WORK(rfnm_hard_reset_work, rfnm_hard_reset_work_fn);

int rfnm_schedule_hard_reset_la9310(uint64_t dcs_freq) {
	if(!dcs_freq) {
		return -EINVAL;
	}

	if(atomic_cmpxchg(&rfnm_hard_reset_running, 0, 1) != 0) {
		return -EBUSY;
	}

	WRITE_ONCE(rfnm_hard_reset_dcs_freq, dcs_freq);
	WRITE_ONCE(rfnm_hard_reset_status, RFNM_API_TIMEOUT);
	if(!schedule_work(&rfnm_hard_reset_work)) {
		atomic_set(&rfnm_hard_reset_running, 0);
		WRITE_ONCE(rfnm_hard_reset_status, RFNM_API_PROBE_FAIL);
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rfnm_schedule_hard_reset_la9310);

int rfnm_get_hard_reset_la9310_status(void) {
	if(atomic_read(&rfnm_hard_reset_running)) {
		return RFNM_API_TIMEOUT;
	}

	return READ_ONCE(rfnm_hard_reset_status);
}
EXPORT_SYMBOL_GPL(rfnm_get_hard_reset_la9310_status);


#include <linux/poll.h>
//RFNM_SYSCTL_TRANSFER_SIZE



static unsigned int rfnm_dev_poll(struct file *file, poll_table *wait)
{
    struct rfnm_dev *dev = file->private_data;
    unsigned int mask = 0;

    rfnm_local_last_consume_jiffies = jiffies;

    /* hook up the user’s wait to our queues */
    poll_wait(file, &local_rx_poll, wait);
    poll_wait(file, &local_tx_poll, wait);

    /* if there’s at least one RX buffer queued */
    spin_lock(&rfnm_local_buffer_in_user->list_lock);
    if (!list_empty(&rfnm_local_buffer_in_user->active))
        mask |= POLLIN | POLLRDNORM;
    spin_unlock(&rfnm_local_buffer_in_user->list_lock);

    /* if there’s room to queue a TX buffer */
    spin_lock(&rfnm_local_buffer_out_user->list_lock);
    if (!list_empty(&rfnm_local_buffer_out_user->active))
        mask |= POLLOUT | POLLWRNORM;
    spin_unlock(&rfnm_local_buffer_out_user->list_lock);

    return mask;
}




static dev_t rfnm_devnum;
static struct cdev rfnm_cdev;
static struct class *rfnm_dev_class;

static long rfnm_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	cmd = cmd & 0x0ff;
	// No data moves on a dead time generation (RX_PUT stays open - returning a
	// pool slot is cleanup, not a time promise)
	if((cmd == RFNM_LOCAL_RX_GET || cmd == RFNM_LOCAL_TX_ACQ || cmd == RFNM_LOCAL_TX_SUB) && !rfnm_phy_gen_session_ok()) {
		return -ENODEV;
	}
    switch (cmd) {
		case RFNM_LOCAL_RX_GET:
			{
				uint8_t *payload;
				struct rfnm_local_buffer_queue_ele *ele;
				void *stale;
				uint32_t idx;

				rfnm_local_rx_cull_stale();
				ele = rfnm_dequeue_local_buffer_rx_ref(&payload);
				if(ele == NULL) {
					return -ENOSPC;
				}
				idx = ele->addr;
				if(rfnm67_last_addr != 0xFFFFFFFF) {
					uint32_t step = (idx + RFNM_IQFLOOD_LOCAL_RX_BUF_SIZE - rfnm67_last_addr)
							% RFNM_IQFLOOD_LOCAL_RX_BUF_SIZE;
					if(step != 1) {
						rfnm67_canary += (step ? step - 1 : RFNM_IQFLOOD_LOCAL_RX_BUF_SIZE - 1);
					}
				}
				rfnm67_last_addr = idx;

				// the producer assigns fill slots round-robin from its own head, so after
				// a full pool lap a fresh ele can carry the addr of a slot still checked
				// out; the stale handle's data was overwritten either way - retire it
				stale = xchg(&rfnm_local_zc_rx_owned[idx], ele);
				if(stale) {
					rfnm_release_local_buffer_rx_ref(stale);
					rfnm_stream_stats.local_rx_error[1]++;
				}

				if(put_user(idx, (uint32_t __user *)arg)) {
					stale = xchg(&rfnm_local_zc_rx_owned[idx], NULL);
					if(stale) {
						rfnm_release_local_buffer_rx_ref(stale);
					}
					return -EFAULT;
				}
			}
			return 0;
		case RFNM_LOCAL_RX_PUT:
			{
				void *ele;

				if(arg >= RFNM_IQFLOOD_LOCAL_RX_BUF_SIZE) {
					return -EINVAL;
				}
				ele = xchg(&rfnm_local_zc_rx_owned[arg], NULL);
				if(ele == NULL) {
					return -EINVAL;
				}
				rfnm_release_local_buffer_rx_ref(ele);
			}
			return 0;
		case RFNM_LOCAL_TX_ACQ:
			{
				uint8_t *payload;
				struct rfnm_local_buffer_queue_ele *ele;
				void *stale;
				uint32_t idx;

				ele = rfnm_claim_local_buffer_tx(&payload);
				if(ele == NULL) {
					return -ENOSPC;
				}
				rfnm_local_last_consume_jiffies = jiffies;
				idx = ele->addr;

				// TX eles are addr-pinned at init: a resident handle here means the
				// client leaked an acquire; put the old claim back as free
				stale = xchg(&rfnm_local_zc_tx_owned[idx], ele);
				if(stale) {
					rfnm_abort_local_buffer_tx(stale);
				}

				if(put_user(idx, (uint32_t __user *)arg)) {
					stale = xchg(&rfnm_local_zc_tx_owned[idx], NULL);
					if(stale) {
						rfnm_abort_local_buffer_tx(stale);
					}
					return -EFAULT;
				}
			}
			return 0;
		case RFNM_LOCAL_TX_SUB:
			{
				void *ele;

				if(arg >= RFNM_IQFLOOD_LOCAL_TX_BUF_SIZE) {
					return -EINVAL;
				}
				ele = xchg(&rfnm_local_zc_tx_owned[arg], NULL);
				if(ele == NULL) {
					return -EINVAL;
				}
				rfnm_commit_local_buffer_tx(ele);
			}
			return 0;

    default:
        pr_warn("rfnm_dev: unknown ioctl cmd=0x%x\n", cmd);
        return -ENOTTY; /* "Inappropriate ioctl for device" */
    }

    return 0;
}

// mmap of the local packet pools: pgoff selects the pool. Cacheable on purpose - the
// user alias must match the kernel's MEMREMAP_WB alias of the same carve-out (the PIPT
// caches keep the aliases coherent, and the TX consumer's pre-DMA cache clean walks
// physical lines, covering user-written data the same as kernel-written).
static int rfnm_dev_mmap(struct file *file, struct vm_area_struct *vma)
{
	size_t size = vma->vm_end - vma->vm_start;
	phys_addr_t base;
	size_t span;

	if(vma->vm_pgoff == (RFNM_LOCAL_MMAP_RX_OFFSET >> PAGE_SHIFT)) {
		// RX pool is producer-owned: never writable from userspace
		if(vma->vm_flags & VM_WRITE) {
			return -EPERM;
		}
		vm_flags_clear(vma, VM_MAYWRITE);
		base = RFNM_IQFLOOD_LOCAL_RX_MEMADDR;
		span = sizeof(struct rfnm_local_rx_pkt) * RFNM_IQFLOOD_LOCAL_RX_BUF_SIZE;
	} else if(vma->vm_pgoff == (RFNM_LOCAL_MMAP_TX_OFFSET >> PAGE_SHIFT)) {
		base = RFNM_IQFLOOD_LOCAL_TX_MEMADDR;
		span = sizeof(struct rfnm_local_tx_pkt) * RFNM_IQFLOOD_LOCAL_TX_BUF_SIZE;
	} else {
		return -EINVAL;
	}

	if(size > PAGE_ALIGN(span)) {
		return -EINVAL;
	}


	return remap_pfn_range(vma, vma->vm_start, base >> PAGE_SHIFT, size, vma->vm_page_prot);
}

// Hardening: exactly ONE consumer process on the zero-copy data endpoint.
// The pool economy has a single-consumer contract; a second process silently
// steals packets from the live session (proven: one overlapping second consumer
// accounted for an entire packet-loss investigation). Threads of the owner share by design (the lib opens
// one fd per worker); anyone else is refused LOUDLY.
static DEFINE_SPINLOCK(rfnm_data_ep_owner_lock);
static pid_t rfnm_data_ep_owner_tgid;
static int rfnm_dev_open(struct inode *inode, struct file *file)
{
    pid_t tg = task_tgid_nr(current);

    spin_lock(&rfnm_data_ep_owner_lock);
    if (atomic_read(&rfnm_data_ep_openers) > 0 && rfnm_data_ep_owner_tgid != tg) {
        rfnm67_open_refused++;
        spin_unlock(&rfnm_data_ep_owner_lock);
        pr_warn_ratelimited("RFNM: data endpoint busy (owner tgid %d, refused tgid %d comm %s)\n",
            rfnm_data_ep_owner_tgid, tg, current->comm);
        return -EBUSY;
    }
    rfnm_data_ep_owner_tgid = tg;
    atomic_inc(&rfnm_data_ep_openers);
    spin_unlock(&rfnm_data_ep_owner_lock);
    // NO format latch here (deliberate): a bare open is not a session. The local
    // transport's SM-reset ioctl claims CS16; release below restores the default.
    return 0;
}
static int rfnm_dev_release(struct inode *inode, struct file *file)
{
    if (atomic_dec_and_test(&rfnm_data_ep_openers)) {
		spin_lock(&rfnm_data_ep_owner_lock);
		rfnm_data_ep_owner_tgid = 0;
		spin_unlock(&rfnm_data_ep_owner_lock);
		int q;
		void *stale;

		// reclaim pool slots the departed client still held (crashed mid-hold, or
		// exited between GET and PUT) - they sit on no kernel list right now
		for(q = 0; q < RFNM_IQFLOOD_LOCAL_RX_BUF_SIZE; q++) {
			stale = xchg(&rfnm_local_zc_rx_owned[q], NULL);
			if(stale) {
				rfnm_release_local_buffer_rx_ref(stale);
			}
		}
		for(q = 0; q < RFNM_IQFLOOD_LOCAL_TX_BUF_SIZE; q++) {
			stale = xchg(&rfnm_local_zc_tx_owned[q], NULL);
			if(stale) {
				rfnm_abort_local_buffer_tx(stale);
			}
		}

        rfnm_local_rx_fmt = RFNM_PACKET_FMT_PACKED12;
        // last local client gone: silence the air. The fw free-runs the DAC ring, so
        // whatever the client left resident (timed bursts, tone tails) replays every
        // ring lap until the next stream start (~147 re-airs/s measured). The
        // stream-off apply also scrubs, but a client that just exits - or crashes -
        // never sends one. (If a concurrent USB/eth TX stream is live this zeroes
        // its in-flight ring span once - a one-lap glitch, not a stop.)
        rfnm_tx_flush_staging(1);
    }
    return 0;
}

static const struct file_operations rfnm_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = rfnm_dev_ioctl,
	.poll    		= rfnm_dev_poll,
	.mmap			= rfnm_dev_mmap,
    .open           = rfnm_dev_open,
    .release        = rfnm_dev_release,
};


// keep the pending-TX list cc-ordered at insert time: walking back from the tail makes
// in-order arrivals O(1) and the two librfnm workers' submit races displace by at most a
// few entries. The consumer then just takes the head - the old per-consumed-packet
// list_sort walked ~the whole pool depth under this spinlock and capped the duplex feed.
static void rfnm_local_tx_insert_sorted_stamp(struct rfnm_local_buffer_queue_ele *ele) {
	ele->enq_kt = ktime_get();
}

static void rfnm_local_tx_insert_sorted(struct rfnm_local_buffer_queue_ele *ele) {
	rfnm_local_tx_insert_sorted_stamp(ele);
	struct rfnm_local_buffer_queue_ele *cur;

	spin_lock(&rfnm_local_buffer_out->list_lock);
	list_for_each_entry_reverse(cur, &rfnm_local_buffer_out->active, head) {
		if(cur->local_cc <= ele->local_cc) {
			list_add(&ele->head, &cur->head);
			spin_unlock(&rfnm_local_buffer_out->list_lock);
			wake_up(&wq_out);
			return;
		}
	}
	list_add(&ele->head, &rfnm_local_buffer_out->active);
	spin_unlock(&rfnm_local_buffer_out->list_lock);
	// 2 ms hard-lead (r5): every TX enqueue path (USB ingest handoff, eth recv,
	// local write) funnels through here - this wake is what retires the consumer's
	// blind nap. Unconditional: the consumer's wait re-checks the queue anyway,
	// and an empty->nonempty edge test under the lock isn't worth the confusion.
	wake_up(&wq_out);
}

int rfnm_queue_local_buffer_tx(uint8_t *buf, uint32_t len, ktime_t born) {


	struct rfnm_local_buffer_queue_ele *buffer_ele;
	int ret;

	spin_lock(&rfnm_local_buffer_out->list_lock);
	spin_lock(&rfnm_local_buffer_out_user->list_lock);

	buffer_ele = list_first_entry_or_null(&rfnm_local_buffer_out_user->active, struct rfnm_local_buffer_queue_ele, head);

	if (buffer_ele == NULL) {
		spin_unlock(&rfnm_local_buffer_out->list_lock);
		spin_unlock(&rfnm_local_buffer_out_user->list_lock);
		return -ENOSPC;
	}

	list_del(&buffer_ele->head);

	spin_unlock(&rfnm_local_buffer_out->list_lock);
	spin_unlock(&rfnm_local_buffer_out_user->list_lock);

	memcpy(&rfnm_local_buf_tx[buffer_ele->addr].p, buf, len);

	struct rfnm_tx_usb_buf *lb = &rfnm_local_buf_tx[buffer_ele->addr].p;
	buffer_ele->local_cc = lb->usb_cc;
	buffer_ele->born_kt = born;
	//printk("received %d at addr %d\n", lb->usb_cc, buffer_ele->addr);

	rfnm_local_tx_insert_sorted(buffer_ele);

	return 0;
}

EXPORT_SYMBOL(rfnm_queue_local_buffer_tx);

// claim/commit/abort trio: rfnm_eth recvs TCP bytes straight into the pool slot,
// removing the staging-buffer copy + leftover memmove of the queue_local_buffer_tx
// path. claim pops a free slot and hands out its backing buffer; the caller fills
// it (header first, so commit can read usb_cc from it) and commits, or aborts to
// put an unfilled slot back (connection died mid-frame).
void *rfnm_claim_local_buffer_tx(uint8_t **buf) {
	struct rfnm_local_buffer_queue_ele *buffer_ele;

	spin_lock(&rfnm_local_buffer_out->list_lock);
	spin_lock(&rfnm_local_buffer_out_user->list_lock);

	buffer_ele = list_first_entry_or_null(&rfnm_local_buffer_out_user->active, struct rfnm_local_buffer_queue_ele, head);

	if (buffer_ele == NULL) {
		spin_unlock(&rfnm_local_buffer_out->list_lock);
		spin_unlock(&rfnm_local_buffer_out_user->list_lock);
		return NULL;
	}

	list_del(&buffer_ele->head);

	spin_unlock(&rfnm_local_buffer_out->list_lock);
	spin_unlock(&rfnm_local_buffer_out_user->list_lock);

	buffer_ele->born_kt = ktime_get();
	*buf = (uint8_t *) &rfnm_local_buf_tx[buffer_ele->addr].p;

	return buffer_ele;
}

EXPORT_SYMBOL(rfnm_claim_local_buffer_tx);

void rfnm_commit_local_buffer_tx(void *handle) {
	struct rfnm_local_buffer_queue_ele *buffer_ele = handle;
	struct rfnm_tx_usb_buf *lb = &rfnm_local_buf_tx[buffer_ele->addr].p;

	buffer_ele->local_cc = lb->usb_cc;

	rfnm_local_tx_insert_sorted(buffer_ele);
}

EXPORT_SYMBOL(rfnm_commit_local_buffer_tx);

void rfnm_abort_local_buffer_tx(void *handle) {
	struct rfnm_local_buffer_queue_ele *buffer_ele = handle;

	spin_lock(&rfnm_local_buffer_out_user->list_lock);
	list_add_tail(&buffer_ele->head, &rfnm_local_buffer_out_user->active);
	spin_unlock(&rfnm_local_buffer_out_user->list_lock);
}

EXPORT_SYMBOL(rfnm_abort_local_buffer_tx);

int rfnm_dequeue_local_buffer_rx(uint8_t *buf) {
	struct rfnm_local_buffer_queue_ele *buffer_ele;

	rfnm_local_last_consume_jiffies = jiffies;

	spin_lock(&rfnm_local_buffer_in->list_lock);
	spin_lock(&rfnm_local_buffer_in_user->list_lock);

	buffer_ele = list_first_entry_or_null(&rfnm_local_buffer_in_user->active, struct rfnm_local_buffer_queue_ele, head);

	if (buffer_ele == NULL) {
		spin_unlock(&rfnm_local_buffer_in->list_lock);
		spin_unlock(&rfnm_local_buffer_in_user->list_lock);
		return -ENOSPC;
	}

	list_del(&buffer_ele->head);

	spin_unlock(&rfnm_local_buffer_in->list_lock);
	spin_unlock(&rfnm_local_buffer_in_user->list_lock);

	memcpy(buf, &rfnm_local_buf_rx[buffer_ele->addr].p, RFNM_USB_RX_PACKET_SIZE);

	struct rfnm_rx_usb_buf *lb = &rfnm_local_buf_rx[buffer_ele->addr].p;
	//buffer_ele->local_cc = lb->usb_cc;
	//printk("dequeing %d at addr %d\n", lb->usb_cc, buffer_ele->addr);

	spin_lock(&rfnm_local_buffer_in->list_lock);
	list_add_tail(&buffer_ele->head, &rfnm_local_buffer_in->active);
	spin_unlock(&rfnm_local_buffer_in->list_lock);

	return 0;
}

EXPORT_SYMBOL(rfnm_dequeue_local_buffer_rx);

// copy-free variant for rfnm_eth: hands out the packet in place instead of
// memcpying it into a staging buffer. The caller must release the handle once
// the data has been consumed (e.g. after sendmsg copied it into skbs).
void *rfnm_dequeue_local_buffer_rx_ref(uint8_t **buf) {
	struct rfnm_local_buffer_queue_ele *buffer_ele;

	rfnm_local_last_consume_jiffies = jiffies;

	spin_lock(&rfnm_local_buffer_in->list_lock);
	spin_lock(&rfnm_local_buffer_in_user->list_lock);

	buffer_ele = list_first_entry_or_null(&rfnm_local_buffer_in_user->active, struct rfnm_local_buffer_queue_ele, head);

	if (buffer_ele == NULL) {
		spin_unlock(&rfnm_local_buffer_in->list_lock);
		spin_unlock(&rfnm_local_buffer_in_user->list_lock);
		return NULL;
	}

	list_del(&buffer_ele->head);
	rfnm_local_rx_queue_depth--;

	spin_unlock(&rfnm_local_buffer_in->list_lock);
	spin_unlock(&rfnm_local_buffer_in_user->list_lock);

	*buf = (uint8_t *) &rfnm_local_buf_rx[buffer_ele->addr].p;

	return buffer_ele;
}

EXPORT_SYMBOL(rfnm_dequeue_local_buffer_rx_ref);

// Freshness bound (see rfnm_local_rx_queue_bound above): trim the delivery queue
// to the newest N packets, recycling the oldest to the free list, and DISCONT-flag
// the survivor so the client jumps the seam immediately. GET-ioctl path only (same
// lock order as the dequeue above: in, then in_user).
static void rfnm_local_rx_cull_stale(void) {
	struct rfnm_local_buffer_queue_ele *ele;
	int culled = 0;

	if(rfnm_local_rx_queue_bound <= 0) {
		return;
	}

	spin_lock(&rfnm_local_buffer_in->list_lock);
	spin_lock(&rfnm_local_buffer_in_user->list_lock);

	while(rfnm_local_rx_queue_depth > rfnm_local_rx_queue_bound) {
		ele = list_first_entry_or_null(&rfnm_local_buffer_in_user->active, struct rfnm_local_buffer_queue_ele, head);
		if(ele == NULL) {
			break;
		}
		list_del(&ele->head);
		list_add_tail(&ele->head, &rfnm_local_buffer_in->active);
		rfnm_local_rx_queue_depth--;
		culled++;
	}

	if(culled) {
		// the survivor is the seam; its epoch bits are already its own
		ele = list_first_entry_or_null(&rfnm_local_buffer_in_user->active, struct rfnm_local_buffer_queue_ele, head);
		if(ele != NULL) {
			rfnm_local_buf_rx[ele->addr].p.rx_flags |= RFNM_RX_FLAG_DISCONT;
		}
		rfnm_local_rx_culled += culled;
		// culled addr jumps are accounted (counter + DISCONT): re-anchor the theft
		// canary so it keeps watching for UNACCOUNTED slot theft only
		rfnm67_last_addr = 0xFFFFFFFF;
		pr_info_ratelimited("RFNM: local rx lag: culled %d stale packets to depth %d (total %llu)\n",
				culled, rfnm_local_rx_queue_bound, (unsigned long long)rfnm_local_rx_culled);
	}

	spin_unlock(&rfnm_local_buffer_in_user->list_lock);
	spin_unlock(&rfnm_local_buffer_in->list_lock);
}

void rfnm_release_local_buffer_rx_ref(void *handle) {
	struct rfnm_local_buffer_queue_ele *buffer_ele = handle;

	spin_lock(&rfnm_local_buffer_in->list_lock);
	list_add_tail(&buffer_ele->head, &rfnm_local_buffer_in->active);
	spin_unlock(&rfnm_local_buffer_in->list_lock);
}

EXPORT_SYMBOL(rfnm_release_local_buffer_rx_ref);

// lockless on purpose: only used as a wait_event condition (paired with a
// timeout-bounded wait in rfnm_eth), so a racy read is fine
int rfnm_local_buffer_rx_available(void) {
	rfnm_local_last_consume_jiffies = jiffies;
	return !list_empty(&rfnm_local_buffer_in_user->active);
}

EXPORT_SYMBOL(rfnm_local_buffer_rx_available);

// same contract as above, for the TX free pool: wait_event condition only
int rfnm_local_buffer_tx_free(void) {
	return !list_empty(&rfnm_local_buffer_out_user->active);
}

EXPORT_SYMBOL(rfnm_local_buffer_tx_free);




void rfnm_register_reset_dgb_cb(void (*cb)(void)) {
    rfnm_dgb_reset_cb = cb;
}
EXPORT_SYMBOL_GPL(rfnm_register_reset_dgb_cb);

void rfnm_register_lalib_quiesce_cb(void (*cb)(void)) {
	rfnm_lalib_quiesce_cb = cb;
}
EXPORT_SYMBOL_GPL(rfnm_register_lalib_quiesce_cb);


static int __init la9310_rfnm_init(void)
{
	int err = 0, i;
	//struct la9310_dev *la9310_dev;



	for (i = 0; i < UNFLOOD_MAX; i++) {

        ratelimit_state_init(&unflood_rs[i],
                             msecs_to_jiffies(500),
                             3);
    }




	init_completion(&setup_done);

	// bring-up-gate fallback: force-open after 60 s if the boot script never signals
	schedule_delayed_work(&rfnm_bringup_timeout_work, msecs_to_jiffies(60000));

	// The absolute-time owner starts with the module (valid flag defers real
	// bookkeeping until the phytimer callback registers)
	seqlock_init(&rfnm_phy64_lock);
	timer_setup(&rfnm_phy64_timer, rfnm_phy64_timer_fn, 0);
	mod_timer(&rfnm_phy64_timer, jiffies + HZ);
		
	//hrtimer_init(&test_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	//test_hrtimer.function = &test_hrtimer_handler;
	//hrtimer_start(&test_hrtimer, ms_to_ktime(1), HRTIMER_MODE_REL);

	rfnm_dev = kzalloc(sizeof(struct rfnm_dev), GFP_KERNEL);

	dfs_rfnm_dir = debugfs_create_dir("rfnm", NULL);
	dfs_rfnm_stream_stat = debugfs_create_file("stream_status", 0644, dfs_rfnm_dir, NULL, &dfs_rfnm_stream_fops);
	debugfs_create_file("tx_health", 0444, dfs_rfnm_dir, NULL, &dfs_rfnm_tx_health_fops);
	debugfs_create_file("l1trace", 0444, dfs_rfnm_dir, NULL, &dfs_rfnm_l1trace_fops);



	rfnm_dev->usb_config_buffer = kzalloc(CONFIG_DESCRIPTOR_MAX_SIZE, GFP_KERNEL);
	

	/*la9310_dev = get_la9310_dev_byname("nlm0");
	if (la9310_dev == NULL) {
		pr_err("No LA9310 device named nlm0\n");
		return -ENODEV;
	}*/

	memset(&rfnm_stream_stats, 0x00, sizeof(struct rfnm_stream_stats));

	tmp_usb_buffer_copy_to_be_deprecated =  kzalloc(500*1000, GFP_KERNEL);

/*
	for(i = 0; i < RFNM_IQFLOOD_BUFCNT; i++) {
		rfnm_iqflood_buf[i] = kmalloc(RFNM_IQFLOOD_BUFSIZE, GFP_KERNEL);
		if(!rfnm_iqflood_buf[i]) {
			dev_err(la9310_dev->dev, "Failed to allocate memory for I/Q buffer\n");
			err = ENOMEM;
		}
	}
*/	
	//rfnm_iqflood_vmem_nocache = ioremap(RFNM_IQFLOOD_MEMADDR, RFNM_IQFLOOD_MEMSIZE);
	//rfnm_iqflood_vmem = memremap(RFNM_IQFLOOD_MEMADDR, RFNM_IQFLOOD_MEMSIZE, MEMREMAP_WB ); 

	//if(!rfnm_iqflood_vmem) {
	//	dev_err(la9310_dev->dev, "Failed to map I/Q buffer\n");
	//	err = ENOMEM;
	//}

	//printk("Mapped IQflood from %x to %p\n", RFNM_IQFLOOD_MEMADDR, rfnm_iqflood_vmem);


	gpio4_iomem = ioremap(0x30230000, SZ_4K);
	gpio4 = (volatile unsigned int *) gpio4_iomem;
	gpio4_initial = *gpio4;

	// disable gpio4
	gpio4 = kzalloc(SZ_4K, GFP_KERNEL);

	rfnm_bufdesc_rx = (struct rfnm_bufdesc_rx *) memremap(RFNM_IQFLOOD_ADC_MEMADDR, SZ_64M, MEMREMAP_WB);
	printk("Mapped rfnm_bufdesc_rx from %x to %lx size %d\n", RFNM_IQFLOOD_ADC_MEMADDR, rfnm_bufdesc_rx, (sizeof(struct rfnm_bufdesc_rx) * RFNM_ADC_BUFCNT));

	rfnm_bufdesc_tx = (struct rfnm_bufdesc_tx *) memremap(RFNM_IQFLOOD_DAC_MEMADDR, SZ_64M, MEMREMAP_WB);
	printk("Mapped rfnm_bufdesc_tx from %x to %lx size %d\n", RFNM_IQFLOOD_DAC_MEMADDR, rfnm_bufdesc_tx, (sizeof(struct rfnm_bufdesc_tx) * RFNM_DAC_BUFCNT));
	//rfnm_rx_usb_buf = kzalloc((sizeof(struct rfnm_rx_usb_buf) * RFNM_RX_USB_BUF_SIZE), GFP_KERNEL);

	rfnm_rx_usb_buf = (struct rfnm_rx_usb_buf *) memremap(
						RFNM_IQFLOOD_USB_MEMADDR, sizeof(struct rfnm_rx_usb_buf) * RFNM_RX_USB_BUF_SIZE, MEMREMAP_WB);

	printk("Mapped rfnm_rx_usb_buf from %x to %lx size %d\n", 
						RFNM_IQFLOOD_USB_MEMADDR, rfnm_rx_usb_buf, sizeof(struct rfnm_rx_usb_buf) * RFNM_RX_USB_BUF_SIZE);

	rfnm_local_buf_rx = (struct rfnm_local_rx_pkt *) memremap(
						RFNM_IQFLOOD_LOCAL_RX_MEMADDR, sizeof(struct rfnm_local_rx_pkt) * RFNM_IQFLOOD_LOCAL_RX_BUF_SIZE, MEMREMAP_WB);

	printk("Mapped rfnm_local_buf_rx from %x to %lx size %d\n", 
						RFNM_IQFLOOD_LOCAL_RX_MEMADDR, rfnm_local_buf_rx, sizeof(struct rfnm_local_rx_pkt) * RFNM_IQFLOOD_LOCAL_RX_BUF_SIZE);

	rfnm_local_buf_tx = (struct rfnm_local_tx_pkt *) memremap(
						RFNM_IQFLOOD_LOCAL_TX_MEMADDR, sizeof(struct rfnm_local_tx_pkt) * RFNM_IQFLOOD_LOCAL_TX_BUF_SIZE, MEMREMAP_WB);

	printk("Mapped rfnm_local_buf_tx from %x to %lx size %d\n", 
						RFNM_IQFLOOD_LOCAL_TX_MEMADDR, rfnm_local_buf_tx, sizeof(struct rfnm_local_tx_pkt) * RFNM_IQFLOOD_LOCAL_TX_BUF_SIZE);

	// the two local rings live at fixed carve-outs 0x82400000/0x87400000; the grown
	// cs16 elements must stay inside the 0x5000000 stride between them
	BUILD_BUG_ON(sizeof(struct rfnm_local_rx_pkt) * RFNM_IQFLOOD_LOCAL_RX_BUF_SIZE > 0x5000000);
	BUILD_BUG_ON(sizeof(struct rfnm_local_tx_pkt) * RFNM_IQFLOOD_LOCAL_TX_BUF_SIZE > 0x5000000);


	//rfnm_rx_usb_buf = kzalloc((sizeof(struct rfnm_rx_usb_buf) * RFNM_RX_USB_BUF_SIZE), GFP_KERNEL);
	//rfnm_m7_status = (struct rfnm_m7_status *) ioremap((0x00900000 + 0x1000), SZ_4K);

	rfnm_la9310_status = (struct rfnm_la9310_status *)  ioremap((RFNM_IQFLOOD_STATUS_MEMADDR), SZ_4K);

	
	tmp_buff_uncompress = kzalloc(4096*1, GFP_KERNEL);



	//spin_lock_init(&rfnm_dev->rx_usb_cb.reader_lock);
	//spin_lock_init(&rfnm_dev->rx_usb_cb.writer_lock);




printk("RFNM_USB_RX_PACKET_SIZE is %d\n", RFNM_USB_RX_PACKET_SIZE);
	

	rfnm_reset_sm();
	

	rfnm_usb_req_buffer_in = kzalloc(sizeof(struct rfnm_usb_req_buffer), GFP_KERNEL);
	if (!rfnm_usb_req_buffer_in) {
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&rfnm_usb_req_buffer_in->active);

	rfnm_usb_req_inflight_in = kzalloc(sizeof(struct rfnm_usb_req_buffer), GFP_KERNEL);
	if (!rfnm_usb_req_inflight_in) {
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&rfnm_usb_req_inflight_in->active);
	spin_lock_init(&rfnm_usb_req_inflight_in->list_lock);

	rfnm_usb_req_buffer_in_usb = kzalloc(sizeof(struct rfnm_usb_req_buffer), GFP_KERNEL);
	if (!rfnm_usb_req_buffer_in_usb) {
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&rfnm_usb_req_buffer_in_usb->active);

	rfnm_usb_req_buffer_out_usb = kzalloc(sizeof(struct rfnm_usb_req_buffer), GFP_KERNEL);
	if (!rfnm_usb_req_buffer_out_usb) {
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&rfnm_usb_req_buffer_out_usb->active);

	rfnm_usb_req_buffer_out = kzalloc(sizeof(struct rfnm_usb_req_buffer), GFP_KERNEL);
	if (!rfnm_usb_req_buffer_out) {
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&rfnm_usb_req_buffer_out->active);

	
	spin_lock_init(&rfnm_usb_req_buffer_in->list_lock);
	spin_lock_init(&rfnm_usb_req_buffer_out->list_lock);
	spin_lock_init(&rfnm_usb_req_buffer_in_usb->list_lock);
	spin_lock_init(&rfnm_usb_req_buffer_out_usb->list_lock);


	rfnm_local_buffer_in = kzalloc(sizeof(struct rfnm_local_buffer), GFP_KERNEL);
	if (!rfnm_local_buffer_in) {
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&rfnm_local_buffer_in->active);

	rfnm_local_buffer_in_user = kzalloc(sizeof(struct rfnm_local_buffer), GFP_KERNEL);
	if (!rfnm_local_buffer_in_user) {
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&rfnm_local_buffer_in_user->active);

	rfnm_local_buffer_out = kzalloc(sizeof(struct rfnm_local_buffer), GFP_KERNEL);
	if (!rfnm_local_buffer_out) {
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&rfnm_local_buffer_out->active);

	rfnm_local_buffer_out_user = kzalloc(sizeof(struct rfnm_local_buffer), GFP_KERNEL);
	if (!rfnm_local_buffer_out_user) {
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&rfnm_local_buffer_out_user->active);

	spin_lock_init(&rfnm_local_buffer_in->list_lock);
	spin_lock_init(&rfnm_local_buffer_in_user->list_lock);
	spin_lock_init(&rfnm_local_buffer_out->list_lock);
	spin_lock_init(&rfnm_local_buffer_out_user->list_lock);


	

	

	for(int q = 0; q < RFNM_IQFLOOD_LOCAL_RX_BUF_SIZE; q++) {
		struct rfnm_local_buffer_queue_ele *new_ele;
		new_ele = kzalloc(sizeof(struct rfnm_local_buffer_queue_ele), GFP_KERNEL);
		//new_ele->addr = q;
		new_ele->owner = 0;

		list_add_tail(&new_ele->head, &rfnm_local_buffer_in->active);
	}

	for(int q = 0; q < RFNM_IQFLOOD_LOCAL_TX_BUF_SIZE; q++) {
		struct rfnm_local_buffer_queue_ele *new_ele;
		new_ele = kzalloc(sizeof(struct rfnm_local_buffer_queue_ele), GFP_KERNEL);
		new_ele->addr = q;
		new_ele->owner = 0;

		list_add_tail(&new_ele->head, &rfnm_local_buffer_out_user->active);
	}
	

	init_waitqueue_head(&local_rx_poll);
    init_waitqueue_head(&local_tx_poll);

   


	
	/*err = usb_gadget_probe_driver(&rfnm_usb_driver);
	if (err < 0)
		dev_err(la9310_dev->dev, "Failed to register USB driver\n");*/


	// callback should be called when certain everything is inited
	/*err = rfnm_callback_init(la9310_dev);
	if (err < 0)
		dev_err(la9310_dev->dev, "Failed to register RFNM Callback\n");*/



	//tasklet_schedule(&rfnm_tasklet_in);

	//start_sm();
	schedule_work(&start_sm_wq);

#if 1


	int list[] = {
			RFNM_DGB_GPIO4_0,
			RFNM_DGB_GPIO4_1,
			RFNM_DGB_GPIO4_2,
			RFNM_DGB_GPIO4_3,
			RFNM_DGB_GPIO4_4,
			RFNM_DGB_GPIO4_5,
			RFNM_DGB_GPIO4_6,
			RFNM_DGB_GPIO4_7, 
			RFNM_DGB_GPIO4_8};

	for(i = 0; i < 9; i++) {		
		if(GPIO_DEBUG) rfnm_gpio_output(0, list[i]);	
		if(GPIO_DEBUG) rfnm_gpio_set(0, list[i]);		
		if(GPIO_DEBUG) rfnm_gpio_clear(0, list[i]);	
	}
#endif


	// unusual but hey

	err = alloc_chrdev_region(&rfnm_devnum, 0, 1, "rfnm_data_ep");
    if (err < 0) {
        printk("rfnm_dev: alloc_chrdev_region failed\n");
        return err;
    }

    cdev_init(&rfnm_cdev, &rfnm_fops);
    rfnm_cdev.owner = THIS_MODULE;
    err = cdev_add(&rfnm_cdev, rfnm_devnum, 1);
    if (err) {
        printk("rfnm_dev: cdev_add failed\n");
        unregister_chrdev_region(rfnm_devnum, 1);
        return err;
    }

	printk("rfnm_dev: loaded. Major=%d Minor=%d\n",
            MAJOR(rfnm_devnum), MINOR(rfnm_devnum));


	rfnm_dev_class = class_create("rfnm_data");
	device_create(rfnm_dev_class, NULL, rfnm_devnum, NULL, "rfnm_data_ep");


	return err;
}

static void  __exit la9310_rfnm_exit(void)
{
	cancel_delayed_work_sync(&rfnm_bringup_timeout_work);
	del_timer_sync(&rfnm_phy64_timer);
	int err = 0, i;
	/*struct la9310_dev *la9310_dev = get_la9310_dev_byname("nlm0");

	if (la9310_dev == NULL) {
		pr_err("No LA9310 device name found during %s\n", __func__);
		return;
	}*/

	/*err = rfnm_callback_deinit();
	if (err < 0)
		dev_err(la9310_dev->dev, "Failed to unregister V2H Callback\n");*/

	

	cancel_work_sync(&rfnm_hard_reset_work);
	cancel_work_sync(&rfnm_restart_sm_work);
	// The partial-module-reload oops class: a queued starter must not spawn workers under
	// teardown, and returning from module_exit with a worker still alive frees module
	// text under a running kthread (module_exit cannot veto the unload - the "leak
	// instead" idea oopsed exactly that way). Wait as long as it takes: a visibly hung
	// rmmod is honest and recoverable; freed-text execution is neither. With the cb
	// retract-and-settle fix the historical phantom counts (a worker killed mid-call
	// by an unretracted pointer never decrements workers_alive) no longer happen.
	flush_work(&start_sm_wq);
	{
		int waits = 0;

		while(stop_sm()) {
			pr_err("RFNM: exit: stream workers still alive after ~%d s - waiting (rmmod blocks until they stop)\n", ++waits * 2);
		}
	}

	kfree(rfnm_dev);
	kfree(tmp_usb_buffer_copy_to_be_deprecated);
	//kfree(rfnm_rx_usb_buf);
	

	//tasklet_kill(&rfnm_tasklet_in);

	debugfs_remove_recursive(dfs_rfnm_dir);

	/*usb_gadget_unregister_driver(&rfnm_usb_driver);*/


	//hrtimer_cancel(&test_hrtimer);


	device_destroy(rfnm_dev_class, rfnm_devnum);
    class_destroy(rfnm_dev_class);
	cdev_del(&rfnm_cdev);
    unregister_chrdev_region(rfnm_devnum, 1);


}

MODULE_PARM_DESC(device, "LA9310 Device name(wlan_monX)");
module_init(la9310_rfnm_init);
module_exit(la9310_rfnm_exit);
MODULE_LICENSE("GPL");
