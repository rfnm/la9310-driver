#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/dma-mapping.h>
#include <la9310_base.h>
#include "la9310_rfnm.h"
#include "la9310_rfnm_callback.h"
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

#include "/home/davide/imx-rfnm-bsp/build/tmp/work-shared/imx8mp-rfnm/kernel-source/drivers/usb/gadget/function/g_zero.h"
#include "/home/davide/imx-rfnm-bsp/build/tmp/work-shared/imx8mp-rfnm/kernel-source/drivers/usb/gadget/u_f.h"

#include <linux/delay.h>

#define RFNM_ADC_BUFCNT (4096*4) // 4096 ~= 10ms



volatile int countdown_to_print = 0;

volatile int callback_cnt = 0;
volatile int last_callback_cnt = 0;
volatile int received_data = 0;
volatile int last_received_data = 0;
volatile int last_rcv_buf = 0;
volatile int dropped_count = 0;
volatile long long int last_print_time = 0;
volatile long long int total_processing_time = 0;
volatile long long int last_processing_time = 0;

uint8_t * tmp_usb_buffer_copy_to_be_deprecated;

#define RFNM_IQFLOOD_BUFSIZE (1024*1024*2)
#define RFNM_IQFLOOD_CBSIZE (RFNM_IQFLOOD_BUFSIZE * 8)

void __iomem *gpio4_iomem;
volatile unsigned int *gpio4;
int gpio4_initial;

//uint8_t * rfnm_iqflood_vmem;
//uint8_t * rfnm_iqflood_vmem_nocache;

#define RFNM_PACKED_STRUCT( __Declaration__ ) __Declaration__ __attribute__((__packed__))

typedef uint32_t vspa_complex_fixed16;
#define FFT_SIZE 512
#define DMA_RX_SIZE		(256)
#define LA_RX_BASE_BUFSIZE (4*DMA_RX_SIZE)
#define LA_RX_BASE_BUFSIZE_12 ((LA_RX_BASE_BUFSIZE * 3) / 4)

#define RFNM_RX_BUF_CNT 7
#define RFNM_IGN_BUFCNT 3
#define ERROR_MAX 0x9
RFNM_PACKED_STRUCT(
	struct rfnm_m7_status {
		uint32_t tx_buf_id;
		uint32_t rx_head;
		uint32_t kernel_cache_flush_tail; // this variable shouldn't be here, but it's already mapped ... 
	} 
); 

struct rfnm_bufdesc_tx {
	vspa_complex_fixed16 buf[FFT_SIZE];
	uint32_t dac_id;
	uint32_t phytimer;
	uint32_t cc;
	uint32_t axiq_done;
	uint32_t iqcomp_done;
	// (64 - (4 * 5)) / 4 = 22
	uint32_t pad_to_64[11];
};

struct rfnm_bufdesc_rx {
	vspa_complex_fixed16 buf[DMA_RX_SIZE];
	uint32_t adc_id;
	uint32_t phytimer;
	uint32_t cc;
	uint32_t axiq_done;
	uint32_t iqcomp_done;
	uint32_t read;
	// (64 - (4 * 5)) / 4 = 22
	uint32_t pad_to_64[10];
};

struct rfnm_bufdesc_rx *rfnm_bufdesc_rx;
volatile struct rfnm_m7_status *rfnm_m7_status;

struct rfnm_rx_usb_cb {
	// in the buffer of rx_usb_cb outgoing usb buffers, this is the next one we are going to equeue
	// there is no tail; it's meant to overflow
	uint32_t head;
	uint32_t adc_buf[4];
	uint32_t adc_buf_cnt[4];
	uint32_t cc;
	uint32_t usb_host_dropped;
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
	
	//int reader_too_slow;
	//int writer_too_slow;
	spinlock_t writer_lock;
	spinlock_t reader_lock;
	//int read_cc;
};

#define RFNM_RX_USB_BUF_MULTI 128
#define RFNM_RX_USB_BUF_SIZE 128

RFNM_PACKED_STRUCT(
    struct rfnm_rx_usb_buf {
        uint32_t magic;
		uint32_t phytimer;
		uint32_t dropped;
		uint32_t adc_cc;
		uint32_t rx_cc;
		uint32_t adc_id;
		uint32_t padding[2];
		uint8_t buf[LA_RX_BASE_BUFSIZE_12 * RFNM_RX_USB_BUF_MULTI];
    }
);

struct rfnm_rx_usb_buf *rfnm_rx_usb_buf;

struct rfnm_usb_req_buffer {
	spinlock_t list_lock;
	struct list_head active;
};

struct rfnm_usb_req_buffer *rfnm_usb_req_buffer;

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
};

struct rfnm_dev {
	struct rfnm_rx_usb_cb rx_usb_cb;
	struct rfnm_rx_la_cb rx_la_cb;
	uint8_t * usb_config_buffer;
};

#define CONFIG_DESCRIPTOR_MAX_SIZE 1000

struct rfnm_dev *rfnm_dev;


#define DRIVER_VENDOR_ID	0x0525 /* NetChip */
#define DRIVER_PRODUCT_ID	0xc0de /* undefined */

#define USB_DEBUG_MAX_PACKET_SIZE     8
#define DBGP_REQ_EP0_LEN              128
#define DBGP_REQ_LEN                  512


void rfnm_pack16to12_aarch64(uint8_t * dest, uint8_t * src8, int cnt);
#if 0
void pack16to12(uint8_t* dest, uint8_t* src8, int cnt) {
	uint64_t buf;
	uint64_t r0;
	int32_t c;
	uint64_t* dest_64;
	uint64_t* src;

	src = (uint64_t*)src8;

	cnt = cnt / 8;

	//printk("%p %p\n", dest, src);


	for (c = 0; c < cnt; c++) {
		buf = *(src + c);
		r0 = 0;
		r0 |= (buf & (0xfffll << 4)) >> 4;
		r0 |= (buf & (0xfffll << 20)) >> 8;
		r0 |= (buf & (0xfffll << 36)) >> 12;
		r0 |= (buf & (0xfffll << 52)) >> 16;

		//printk("set %llux to %p (c=%d)\n", r0,  ( void * ) (dest + (c * 3)), c);
		dest_64 = (uint64_t*)(dest + (c * 6));
		*dest_64 = r0;

		//if(c > 10)
		//	return;
	}

}

#endif

static inline size_t list_count_nodes(struct list_head *head)
{
	struct list_head *pos;
	size_t count = 0;

	list_for_each(pos, head)
		count++;

	return count;
}

struct __attribute__((__packed__)) rfnm_packet_head {
		uint32_t check;
	uint32_t cc;
	uint8_t reader_too_slow;
	uint8_t padding[16 - 9 + 4 + 12];
};

#define RFNM_PACKET_HEAD_SIZE sizeof (struct rfnm_packet_head)


static void rfnm_usb_buffer_done(struct usb_ep_queue_ele *usb_ep_queue_ele)
{
	unsigned long flags;
	int status;

	spin_lock_irqsave(&rfnm_usb_req_buffer->list_lock, flags);
	list_del(&usb_ep_queue_ele->head);
	spin_unlock_irqrestore(&rfnm_usb_req_buffer->list_lock, flags);

	status = usb_ep_queue(usb_ep_queue_ele->ep, usb_ep_queue_ele->req, GFP_ATOMIC);
	if (status) {
		printk("kill %s:  resubmit %d bytes --> %d\n",usb_ep_queue_ele->ep->name, usb_ep_queue_ele->req->length, status);
		usb_ep_set_halt(usb_ep_queue_ele->ep);
		// FIXME recover later ... somehow 
	}

	kfree(usb_ep_queue_ele);
}


#if 0
static void rfnm_tasklet_handler(unsigned long tasklet_data) {

	struct usb_ep_queue_ele *usb_ep_queue_ele;

tasklet_again:

	usb_ep_queue_ele = list_first_entry_or_null(&rfnm_usb_req_buffer->active, struct usb_ep_queue_ele, head);

	if(usb_ep_queue_ele == NULL) {
		*gpio4 = *gpio4 | (0x1 << 8); *gpio4 = *gpio4 & ~(0x1 << 8);
		goto exit_tasklet_no_unlock;
		return;
	}

	uint8_t * usb_buf_ptr = (uint8_t *) usb_ep_queue_ele->req->buf;

	spin_lock(&rfnm_dev->rx_usb_cb.reader_lock);

	int head = smp_load_acquire(&rfnm_dev->rx_usb_cb.head);
	int tail = rfnm_dev->rx_usb_cb.tail;
	int readable, writable;
	//uint8_t * block_writing_to_addr = usb_buf_ptr;
	
	writable = usb_ep_queue_ele->req->length - RFNM_PACKET_HEAD_SIZE;
	readable = head - tail;

	if(head < tail) {
		readable += RFNM_IQFLOOD_CBSIZE; 
	}

	if(readable % 8) {
		printk("readable is not divisible by 8\n", readable);
	}

	if(tail % 8) {
		printk("tail is not divisible by 8\n", tail);
	}

	readable = (readable * 12) / 16;

	if(readable % 6) {
		printk("readable is not divisible by 6\n", readable);
	}

	//printk("e %d %d %d", head, tail, readable);
	
	
	if(readable < writable) {
		// we only want full outgoing buffers but it's not tecnically a requirement
		//*gpio4 = *gpio4 | (0x1 << 9); *gpio4 = *gpio4 & ~(0x1 << 9);
		goto exit_tasklet;
	}
	
	*gpio4 = *gpio4 | (0x1 << 9); 


	/*int z;
	for(z = 0; z < 0x80; z++) {
		*(rfnm_iqflood_vmem + tail + z) = z * 0x11;
	}*/

//udelay(100);
	if(tail + ((writable * 16) / 12) > RFNM_IQFLOOD_CBSIZE) {	
		unsigned long first_read_size = RFNM_IQFLOOD_CBSIZE - tail;

		unsigned long start;

		/*start = (unsigned long) rfnm_iqflood_vmem + tail;
		dcache_inval_poc(start, start + first_read_size);

		start = (unsigned long) usb_buf_ptr + RFNM_PACKET_HEAD_SIZE;
		dcache_inval_poc(start, start + first_read_size);*/

		//memset((uint8_t *)rfnm_iqflood_vmem + tail, 0xee, 100);
		//memset(usb_buf_ptr + RFNM_PACKET_HEAD_SIZE, 0xdd, 100);

		// cnt param is wrong in more ways than I can count

		/*if(first_read_size % 3)
			printk("first_read_size %d is not divisible by 3\n", first_read_size);

		if((writable - first_read_size) % 3)
			printk("writable - first_read_size %d is not divisible by 3\n", writable - first_read_size);

		pack16to12((uint8_t *) usb_buf_ptr + RFNM_PACKET_HEAD_SIZE, (uint8_t *) rfnm_iqflood_vmem + tail, (first_read_size * 16) / 12);
		pack16to12((uint8_t *) usb_buf_ptr + RFNM_PACKET_HEAD_SIZE + first_read_size, (uint8_t *) rfnm_iqflood_vmem, (writable - first_read_size * 16) / 12);
		tail += ((writable * 16) / 12) - RFNM_IQFLOOD_CBSIZE;*/





		// using a temporary copy buffer is the cleanest way to do this:
		// can avoid adding a size param to the buffer header (which we might need to anyway for specs reasons)
		// and can avoid rework the packing function to work with non divisible by 3 numbers

		// however, please rework this before shipping the driver (let me guess, you never will do that)
		// at least add size to the specs thanks


		//printk("first read size is %d tail %d, CB %d\n", first_read_size, tail, RFNM_IQFLOOD_CBSIZE);


		memcpy((uint8_t *) tmp_usb_buffer_copy_to_be_deprecated, (uint8_t *) rfnm_iqflood_vmem + tail, first_read_size);
		memcpy((uint8_t *) tmp_usb_buffer_copy_to_be_deprecated + first_read_size, (uint8_t *) rfnm_iqflood_vmem, ((writable * 16) / 12) - first_read_size);

		//printk("%llx %llx %llx %llx\n", tmp_usb_buffer_copy_to_be_deprecated, rfnm_iqflood_vmem + tail, tmp_usb_buffer_copy_to_be_deprecated + first_read_size, rfnm_iqflood_vmem);

		//memcpy((uint8_t *) tmp_usb_buffer_copy_to_be_deprecated, (uint8_t *) rfnm_iqflood_vmem, (writable * 16) / 12);

		pack16to12((uint8_t *) usb_buf_ptr + RFNM_PACKET_HEAD_SIZE, (uint8_t *) tmp_usb_buffer_copy_to_be_deprecated, (writable * 16) / 12);

		tail += ((writable * 16) / 12) - RFNM_IQFLOOD_CBSIZE;




		
	} else {
		//printk("%d\n", tail);
		pack16to12((uint8_t *) usb_buf_ptr + RFNM_PACKET_HEAD_SIZE, (uint8_t *) rfnm_iqflood_vmem + tail, (writable * 16) / 12);
		tail += (writable * 16) / 12;
	}

	

	*gpio4 = *gpio4 & ~(0x1 << 9);
	//printk("o %d %d %d", head, tail, readable);

	//block_writing_to->block.bytes_used = block_writing_to->block.size;


	struct rfnm_packet_head rfnm_packet_head;
	rfnm_packet_head.cc = rfnm_dev->rx_usb_cb.read_cc++;
	rfnm_packet_head.check = 0x7ab8bd6f;

	static int last_reader_slow;

	if(last_reader_slow != rfnm_dev->rx_usb_cb.reader_too_slow) {
		rfnm_packet_head.reader_too_slow = 1;
		last_reader_slow = rfnm_dev->rx_usb_cb.reader_too_slow;
	} else {
		rfnm_packet_head.reader_too_slow = 0;
	}

	memcpy(usb_buf_ptr, &rfnm_packet_head, RFNM_PACKET_HEAD_SIZE); 

	/*memset(usb_buf_ptr + 1000, 0x00, 10000);
	memset(usb_buf_ptr + (usb_ep_queue_ele->req->length/2), 0x00, 5000);
	memset(usb_buf_ptr + usb_ep_queue_ele->req->length - 5000, 0x00, 2500);*/

	if(usb_ep_queue_ele->req->length != ((4096*32))) {
		printk("usb urb is %d\n", usb_ep_queue_ele->req->length);
	}

	dcache_clean_poc(usb_buf_ptr, usb_buf_ptr + usb_ep_queue_ele->req->length);
	
	smp_store_release(&rfnm_dev->rx_usb_cb.tail, tail);

	spin_unlock(&rfnm_dev->rx_usb_cb.reader_lock);

	*gpio4 = *gpio4 | (0x1 << 0); *gpio4 = *gpio4 & ~(0x1 << 0);
	rfnm_usb_buffer_done(usb_ep_queue_ele);

	goto tasklet_again;

exit_tasklet: 
	spin_unlock(&rfnm_dev->rx_usb_cb.reader_lock);
exit_tasklet_no_unlock:
	*gpio4 = *gpio4 & ~(0x1 << 7);
}

#else





#if 0
static void rfnm_tasklet_handler(unsigned long tasklet_data) {

	*gpio4 = *gpio4 | (0x1 << 7); 

tasklet_again:

	//pack16to12((uint8_t *) usb_buf_ptr + RFNM_PACKET_HEAD_SIZE, (uint8_t *) rfnm_iqflood_vmem + tail, (writable * 16) / 12);
	

	//dcache_clean_poc(usb_buf_ptr, usb_buf_ptr + usb_ep_queue_ele->req->length);

//	rfnm_bufdesc_rx

	uint32_t head = rfnm_m7_status->rx_head;
	uint32_t tail = rx_tail;

	uint32_t readable = head - tail;

	if(head < tail) {
		readable += RFNM_ADC_BUFCNT;
	}

	for(int q = 0; q < readable; q++) {

		
		if(rx_cc[rfnm_bufdesc_rx[tail].adc_id] != rfnm_bufdesc_rx[tail].cc) {
			printk("cc mismatch on %d -> %d vs %d\n", rfnm_bufdesc_rx[tail].adc_id, rfnm_bufdesc_rx[tail].cc, rx_cc[rfnm_bufdesc_rx[tail].adc_id]);
			rx_cc[rfnm_bufdesc_rx[tail].adc_id] = rfnm_bufdesc_rx[tail].cc;
		}
		
		rx_cc[rfnm_bufdesc_rx[tail].adc_id]++;
		

		if(++tail == RFNM_ADC_BUFCNT) {
			tail = 0;
		}
	}
	

	printk("head is at %d\n", rfnm_m7_status->rx_head);

//	goto tasklet_again;

exit_tasklet: 
//	spin_unlock(&rfnm_dev->rx_usb_cb.reader_lock);
exit_tasklet_no_unlock:

//	tasklet_schedule(&rfnm_tasklet);

	*gpio4 = *gpio4 & ~(0x1 << 7);

}

#endif

static void rfnm_tasklet_handler(unsigned long tasklet_data);
static DECLARE_TASKLET_OLD(rfnm_tasklet, &rfnm_tasklet_handler);

void kernel_neon_begin(void);
void kernel_neon_end(void);


 static void rfnm_tasklet_handler(unsigned long tasklet_data) {

	*gpio4 = *gpio4 | (0x1 << 4);

	kernel_neon_begin();

	struct usb_ep_queue_ele *usb_ep_queue_ele;
	
tasklet_again:

	uint8_t *dcache;
	
	//*gpio4 = *gpio4 | (0x1 << 7);
	//dcache = (unsigned char *) rfnm_m7_status;
	//dcache_inval_poc(dcache, dcache + SZ_4K);
	//*gpio4 = *gpio4 & ~(0x1 << 7);

	//dcache = (unsigned char *) &rfnm_bufdesc_rx[0];
	//dcache_clean_poc(dcache, dcache + SZ_64M);

	barrier();
	
	//uint32_t la_head = smp_load_acquire(&rfnm_m7_status->rx_head);
	uint32_t la_head = rfnm_m7_status->rx_head;
	uint32_t la_tail = rfnm_dev->rx_la_cb.tail;

	uint32_t la_readable = la_head - la_tail;

	if(la_head < la_tail) {
		la_readable += RFNM_ADC_BUFCNT;
	}

	if(la_readable < 32) {
		// need to stay behind writer, as the ping pong dma has a 2 buffers write latency
		goto exit_tasklet;
	}

	la_readable -= 16;

	if(la_readable < 0) {
		la_readable = 0;
		goto exit_tasklet;
	}

	if(la_readable > (RFNM_ADC_BUFCNT / 4)) {
		// too many buffers behind, log error and jump forward
		rfnm_dev->rx_la_cb.tail = rfnm_m7_status->rx_head;
		printk("too many buffers behind, error not logged to buffer...\n");
		goto exit_tasklet;
	}

	if(la_readable) {
	//	printk("readable %d head %d tail %d\n", la_readable, la_head, la_tail);
	}

	*gpio4 = *gpio4 | (0x1 << 7);

	if(la_tail + la_readable >= RFNM_ADC_BUFCNT) {
		dcache_inval_poc((unsigned char *) &rfnm_bufdesc_rx[la_tail], (unsigned char *) &rfnm_bufdesc_rx[RFNM_ADC_BUFCNT - 1]);
		dcache_inval_poc((unsigned char *) &rfnm_bufdesc_rx[0], (unsigned char *) &rfnm_bufdesc_rx[la_tail - la_readable]);
	} else {
		dcache_inval_poc((unsigned char *) &rfnm_bufdesc_rx[la_tail], (unsigned char *) &rfnm_bufdesc_rx[la_tail + la_readable]);
	}

	
	//dcache = (unsigned char *) &rfnm_bufdesc_rx[la_tail];
	//dcache_inval_poc(dcache, dcache + SZ_64K /*sizeof(struct rfnm_bufdesc_rx)*/);
	*gpio4 = *gpio4 & ~(0x1 << 7);

	barrier();
	
 	
	*gpio4 = *gpio4 | (0x1 << 6);

	for(int q = 0; q < la_readable; q++) {

		//*gpio4 = *gpio4 | (0x1 << 5);
		//*gpio4 = *gpio4 & ~(0x1 << 5);

		uint32_t la_adc_id = smp_load_acquire(&rfnm_bufdesc_rx[la_tail].adc_id);
		uint32_t la_adc_cc = rfnm_bufdesc_rx[la_tail].cc;

		//printk("la_adc_cc %d adc_buf_cnt %d adc_buf %d head %d\n", 
		//	la_adc_cc, rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id], rfnm_dev->rx_usb_cb.adc_buf[la_adc_id], rfnm_dev->rx_usb_cb.head);

		if(la_adc_id > 4) {
			printk("Why is this ADC %d? tail is %d axiq is %d\n", la_adc_id, la_tail, rfnm_bufdesc_rx[la_tail].axiq_done);
			continue;
		}

		
		if(rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id] == RFNM_RX_USB_BUF_MULTI) {
			rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id] = 0;
			rfnm_dev->rx_usb_cb.adc_buf[la_adc_id] = rfnm_dev->rx_usb_cb.head;
			if(++rfnm_dev->rx_usb_cb.head == RFNM_RX_USB_BUF_SIZE) {
				rfnm_dev->rx_usb_cb.head = 0;
			}
		}
#if 1
		//if(q == 0 && rfnm_dev->rx_usb_cb.adc_buf[la_adc_id] == 0)
		//printk("adc_buf %d offset %d destbuf %lx srcbuf %lx\n", rfnm_dev->rx_usb_cb.adc_buf[la_adc_id], LA_RX_BASE_BUFSIZE_12 * rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id], 
		//	&rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].buf[LA_RX_BASE_BUFSIZE_12 * rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id]], rfnm_bufdesc_rx[la_tail].buf);
#endif
#if 0
		
		
		rfnm_pack16to12_aarch64( (uint8_t *) &rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].buf[LA_RX_BASE_BUFSIZE_12 * rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id]], 
					(uint8_t *) rfnm_bufdesc_rx[la_tail].buf, 
					LA_RX_BASE_BUFSIZE / 1);

		

#endif


#if 0
		*gpio4 = *gpio4 | (0x1 << 6);
		
		pack16to12( (uint8_t *) &rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].buf[LA_RX_BASE_BUFSIZE_12 * rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id]], 
					(uint8_t *) rfnm_bufdesc_rx[la_tail].buf, 
					LA_RX_BASE_BUFSIZE / 1);

		*gpio4 = *gpio4 & ~(0x1 << 6);
#endif




		if(!rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id]) {
			//rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].magic = 0x7ab8bd6f;
			//rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].phytimer = rfnm_bufdesc_rx[la_tail].phytimer;
		}


			
		
		if(rfnm_dev->rx_la_cb.adc_cc[la_adc_id] != la_adc_cc) {

			printk("cc mismatch on adc %d -> %d vs %d tail is %d axiq is %d | adc_buf_cnt %d adc_buf %d head %d\n", la_adc_id, 
				la_adc_cc, rfnm_dev->rx_la_cb.adc_cc[la_adc_id], 
				la_tail, rfnm_bufdesc_rx[la_tail].axiq_done,
				rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id], rfnm_dev->rx_usb_cb.adc_buf[la_adc_id], rfnm_dev->rx_usb_cb.head);
			rfnm_dev->rx_la_cb.adc_cc[la_adc_id] = la_adc_cc;
		}
		//la_adc_cc++;
		rfnm_dev->rx_la_cb.adc_cc[la_adc_id]++;

		if(++la_tail == RFNM_ADC_BUFCNT) {
			la_tail = 0;
		}

		rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id]++;
	}

	*gpio4 = *gpio4 & ~(0x1 << 6);
	

	rfnm_dev->rx_la_cb.tail = la_tail;

	rfnm_m7_status->kernel_cache_flush_tail = la_tail;

	
	

	//printk("head is at %d\n", rfnm_m7_status->rx_head);

exit_tasklet: 
	//spin_unlock(&rfnm_dev->rx_usb_cb.reader_lock);
exit_tasklet_no_unlock:
	
	kernel_neon_end();
	*gpio4 = *gpio4 & ~(0x1 << 4);
	tasklet_schedule(&rfnm_tasklet);
}

#endif




void callback_func(struct device *dev)
{
	#if 0
	//struct la9310_dev *la9310_dev = (struct la9310_dev *)cookie;
	
	long long int time_processing_start, time_diff, data_rate, total_processing_time_diff;
	int packet_count_diff, received_data_diff, this_rcv_buf;
	uint32_t* buf_ctrl_packet;
	unsigned long start;
	struct iio_dma_buffer_block * block_writing_to;
	//struct iio_dma_buffer_queue * queue_writing_to;

	//struct iio_rfnm_buffer * list_writing_to;
	
	int list_node_count;
	//struct device *dev;
	//dev = kmalloc(1024, 0);
	//dma_sync_single_for_cpu(dev, rfnm_iqflood_dma_addr, RFNM_IQFLOOD_MEMSIZE, DMA_BIDIRECTIONAL);

	//return;

*gpio4 = *gpio4 | (0x1 << 6); 


	time_processing_start = ktime_get();

	buf_ctrl_packet = (uint32_t*) ((uint8_t*) rfnm_iqflood_vmem_nocache + 1024*1024*17);
	this_rcv_buf = (*buf_ctrl_packet) & 0xf;

	if(last_rcv_buf != this_rcv_buf) {
		last_rcv_buf = this_rcv_buf;
		dropped_count++;
	}

	if(++last_rcv_buf == 8) {
		last_rcv_buf = 0;
	}

	callback_cnt++;


/*
	for(i = 0; i < RFNM_IQFLOOD_BUFSIZE / 4; i++) {
		p =  (uint32_t *)((uint8_t *) rfnm_iqflood_vmem + (this_rcv_buf * RFNM_IQFLOOD_BUFSIZE));
		*(p + i) = 0xfdecfdec;
	}
*/

	// clearing the cache inside the interrupt makes it run for 100x longer, but I don't care for now
	// the other worker thread should clean it chunk by chunk before reading imho
	start = (unsigned long) rfnm_iqflood_vmem + (this_rcv_buf * RFNM_IQFLOOD_BUFSIZE);
	dcache_inval_poc(start, start + RFNM_IQFLOOD_BUFSIZE);

	//pack16to12(rfnm_iqflood_buf[next_iqflood_write_buf], (uint64_t *)((uint8_t *) rfnm_iqflood_vmem + (this_rcv_buf * RFNM_IQFLOOD_BUFSIZE)), RFNM_IQFLOOD_BUFSIZE);
	//memcpy(rfnm_iqflood_buf[next_iqflood_write_buf], ((uint8_t *) rfnm_iqflood_vmem + (this_rcv_buf * RFNM_IQFLOOD_BUFSIZE)), RFNM_IQFLOOD_BUFSIZE / 1);
	//printk("%p %p\n", rfnm_iqflood_buf[next_iqflood_write_buf], rfnm_iqflood_vmem + (this_rcv_buf * RFNM_IQFLOOD_BUFSIZE));

	



	



	//printk("Block retrived! with %llx paddr %p vaddr %lu size\n", block_writing_to->phys_addr, block_writing_to->vaddr, block_writing_to->size);
	
	//pack16to12(rfnm_iqflood_buf[next_iqflood_write_buf], block_writing_to->vaddr, RFNM_IQFLOOD_BUFSIZE);


	spin_lock(&rfnm_dev->rx_usb_cb.writer_lock);

	int head = rfnm_dev->rx_usb_cb.head;
	int tail = READ_ONCE(rfnm_dev->rx_usb_cb.tail);

	if(head < tail) {
		head += RFNM_IQFLOOD_CBSIZE;
	}

	if(head - tail > (RFNM_IQFLOOD_BUFSIZE / 2)) {
		rfnm_dev->rx_usb_cb.reader_too_slow++;
	}

	head = this_rcv_buf * RFNM_IQFLOOD_BUFSIZE;

	smp_store_release(&rfnm_dev->rx_usb_cb.head, head);
	spin_unlock(&rfnm_dev->rx_usb_cb.writer_lock);


	if(1 && ++countdown_to_print >= 100) {
		countdown_to_print = 0;	

		time_processing_start = ktime_get();
		time_diff = time_processing_start - last_print_time;
		last_print_time = time_processing_start;

		packet_count_diff = callback_cnt - last_callback_cnt;
		received_data_diff = received_data - last_received_data;
		total_processing_time_diff = total_processing_time - last_processing_time;

		last_callback_cnt = callback_cnt;
		last_received_data = received_data;
		last_processing_time = total_processing_time;

		received_data_diff = packet_count_diff * RFNM_IQFLOOD_BUFSIZE;

		data_rate = ((received_data_diff / 1000) / (time_diff / (1000 * 1000))) / (1);

		//list_node_count = list_count_nodes(iio_rfnm_active_list_workaround_pointer);


		printk("t %lld (ms) work %lld (us) pkts %d, data %d (MB) rate %lld (MB/s) \ntotal pkts %d rs %d ws %d dropped %d head %d tail %d\n", 
			time_diff / (1000 * 1000), total_processing_time_diff / (1000), packet_count_diff, 
			received_data_diff / (1000*1000), data_rate, callback_cnt, rfnm_dev->rx_usb_cb.reader_too_slow,
			 rfnm_dev->rx_usb_cb.writer_too_slow, dropped_count, rfnm_dev->rx_usb_cb.head, rfnm_dev->rx_usb_cb.tail);

	}

	total_processing_time += ktime_get() - time_processing_start;
	*gpio4 = *gpio4 & ~(0x1 << 6);
	//tasklet_schedule(&rfnm_tasklet);
	#endif
}

static irqreturn_t callback_func_0(int irq, void *dev) {
	callback_func(dev);
	return IRQ_HANDLED;
}

static irqreturn_t callback_func_1(int irq, void *dev) {
	callback_func(dev);
	return IRQ_HANDLED;
}

int rfnm_callback_init(struct la9310_dev *la9310_dev)
{
	int ret = 0;
	dev_info(la9310_dev->dev, "RFNM Callback registered\n");
	ret = register_rfnm_callback((void *)callback_func_0, 0);
	ret = register_rfnm_callback((void *)callback_func_1, 1);
	last_print_time = ktime_get();

	return ret;
}

int unregister_rfnm_callback(void );
int rfnm_callback_deinit(void)
{
	int ret = 0;
	ret = unregister_rfnm_callback();
	return ret;
}





static void rfnm_submit_usb_req(struct usb_ep *ep, struct usb_request *req)
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
	case -ECONNRESET:		/* request dequeued */
	case -ESHUTDOWN:		/* disconnect from host */
		printk("%s dead (%d), %d/%d\n", ep->name, status, req->actual, req->length);
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
		printk( "%s EOVERFLOW (%d), %d/%d\n", ep->name, status, req->actual, req->length);

	default:
#if 1
		rfnm_ep_stats[RFNM_USB_EP_DEFAULT]++;
		printk("%s complete --> %d, %d/%d\n", ep->name, status, req->actual, req->length);
		break;
#endif
	case -EREMOTEIO:		/* short read */
		rfnm_ep_stats[RFNM_USB_EP_REMOTEIO]++;
		printk( "%s short read (%d), %d/%d\n", ep->name, status, req->actual, req->length);
		break;
	}

	struct usb_ep_queue_ele *new_ele;

	new_ele = kzalloc(sizeof(struct rfnm_dev), GFP_KERNEL);
	new_ele->ep = ep;
	new_ele->req = req;

	spin_lock_irq(&rfnm_usb_req_buffer->list_lock);
	list_add_tail(&new_ele->head, &rfnm_usb_req_buffer->active);
	spin_unlock_irq(&rfnm_usb_req_buffer->list_lock);

	//tasklet_schedule(&rfnm_tasklet);






	/*status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		printk("kill %s:  resubmit %d bytes --> %d\n", ep->name, req->length, status);
		usb_ep_set_halt(ep);
		// FIXME recover later ... somehow 
	}*/
}

EXPORT_SYMBOL_GPL(rfnm_submit_usb_req);

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












static int __init la9310_rfnm_init(void)
{
	int err = 0, i;
	struct la9310_dev *la9310_dev;
	rfnm_dev = kzalloc(sizeof(struct rfnm_dev), GFP_KERNEL);

	rfnm_dev->usb_config_buffer = kzalloc(CONFIG_DESCRIPTOR_MAX_SIZE, GFP_KERNEL);
	

	la9310_dev = get_la9310_dev_byname("nlm0");
	if (la9310_dev == NULL) {
		pr_err("No LA9310 device named nlm0\n");
		return -ENODEV;
	}

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

	//dev_info(la9310_dev->dev, "Mapped IQflood from %x to %p\n", RFNM_IQFLOOD_MEMADDR, rfnm_iqflood_vmem);

	gpio4_iomem = ioremap(0x30230000, SZ_4K);
	gpio4 = (volatile unsigned int *) gpio4_iomem;
	gpio4_initial = *gpio4;

	// disable gpio4
	gpio4 = kzalloc(SZ_4K, GFP_KERNEL);

	rfnm_bufdesc_rx = (struct rfnm_bufdesc_rx *) memremap(0x96400000, SZ_64M, MEMREMAP_WB);

	//rfnm_rx_usb_buf = (struct rfnm_rx_usb_buf *) memremap(0x96400000 + (sizeof(struct rfnm_rx_usb_buf) * RFNM_ADC_BUFCNT), SZ_256M, MEMREMAP_WB);

	dev_info(la9310_dev->dev, "Mapped rfnm_bufdesc_rx from %x to %lx size %d\n", 0x96400000, rfnm_bufdesc_rx, SZ_64M);

	rfnm_rx_usb_buf = kzalloc((sizeof(struct rfnm_rx_usb_buf) * RFNM_RX_USB_BUF_SIZE), GFP_KERNEL);

	dev_info(la9310_dev->dev, "Mapped rfnm_rx_usb_buf from %x to %lx size %d\n", 0x96400000 + (sizeof(struct rfnm_rx_usb_buf) * RFNM_ADC_BUFCNT), rfnm_rx_usb_buf, (sizeof(struct rfnm_rx_usb_buf) * RFNM_RX_USB_BUF_SIZE));

	//rfnm_m7_status = (struct rfnm_m7_status *) memremap((0x00900000 + 0x1000), SZ_4K, MEMREMAP_WB);
	rfnm_m7_status = (struct rfnm_m7_status *) ioremap((0x00900000 + 0x1000), SZ_4K);



	spin_lock_init(&rfnm_dev->rx_usb_cb.reader_lock);
	spin_lock_init(&rfnm_dev->rx_usb_cb.writer_lock);

	rfnm_dev->rx_usb_cb.head = 0;
	rfnm_dev->rx_usb_cb.cc = 0;
	rfnm_dev->rx_usb_cb.usb_host_dropped = 0;
	rfnm_dev->rx_la_cb.tail = 0;
	
	for(i = 0; i < 4; i++) {
		rfnm_dev->rx_usb_cb.adc_buf[i] = 0;
		rfnm_dev->rx_usb_cb.adc_buf_cnt[i] = RFNM_RX_USB_BUF_MULTI;
		rfnm_dev->rx_la_cb.adc_cc[i] = 0;
	}

	rfnm_usb_req_buffer = kzalloc(sizeof(*rfnm_usb_req_buffer), GFP_KERNEL);
	if (!rfnm_usb_req_buffer)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&rfnm_usb_req_buffer->active);

	

	
	/*err = usb_gadget_probe_driver(&rfnm_usb_driver);
	if (err < 0)
		dev_err(la9310_dev->dev, "Failed to register USB driver\n");*/


	// callback should be called when certain everything is inited
	err = rfnm_callback_init(la9310_dev);
	if (err < 0)
		dev_err(la9310_dev->dev, "Failed to register RFNM Callback\n");


	tasklet_schedule(&rfnm_tasklet);

	return err;
}

static void  __exit la9310_rfnm_exit(void)
{
	int err = 0, i;
	struct la9310_dev *la9310_dev = get_la9310_dev_byname("nlm0");

	if (la9310_dev == NULL) {
		pr_err("No LA9310 device name found during %s\n", __func__);
		return;
	}

	err = rfnm_callback_deinit();
	if (err < 0)
		dev_err(la9310_dev->dev, "Failed to unregister V2H Callback\n");

	kfree(rfnm_dev);
	kfree(tmp_usb_buffer_copy_to_be_deprecated);
	kfree(rfnm_rx_usb_buf);

	tasklet_kill(&rfnm_tasklet);

	/*usb_gadget_unregister_driver(&rfnm_usb_driver);*/
}

MODULE_PARM_DESC(device, "LA9310 Device name(wlan_monX)");
module_init(la9310_rfnm_init);
module_exit(la9310_rfnm_exit);
MODULE_LICENSE("GPL");




