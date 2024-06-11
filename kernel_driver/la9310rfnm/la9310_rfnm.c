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

#include "drivers/usb/gadget/function/g_zero.h"
#include "drivers/usb/gadget/u_f.h"

#include <linux/delay.h>

#include <linux/debugfs.h>

#include <linux/rfnm-shared.h>
#include <linux/rfnm-vspa.h>
#include <linux/rfnm-api.h>
#include <linux/rfnm-gpio.h>

#include <linux/kthread.h>
#include <linux/list_sort.h>

#include <uapi/linux/sched.h>
#include <uapi/linux/sched/types.h>

#include <linux/sched.h>

#define GPIO_DEBUG 0

DECLARE_WAIT_QUEUE_HEAD(wq_out);
DECLARE_WAIT_QUEUE_HEAD(wq_in);
DECLARE_WAIT_QUEUE_HEAD(wq_usb);

#define RFNM_ADC_BUFCNT (0x4000) // 4096 ~= 10ms



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
volatile struct rfnm_m7_status *rfnm_m7_status;

struct rfnm_rx_usb_cb {
	// in the buffer of rx_usb_cb outgoing usb buffers, this is the next one we are going to equeue
	// there is no tail; it's meant to overflow
	uint32_t head;
	uint32_t adc_buf[4];
	uint32_t adc_buf_cnt[4];
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

struct rfnm_rx_usb_buf *rfnm_rx_usb_buf;



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



struct rfnm_stream_stats rfnm_stream_stats;



struct rfnm_usb_req_buffer *rfnm_usb_req_buffer_in;
struct rfnm_usb_req_buffer *rfnm_usb_req_buffer_in_usb;
struct rfnm_usb_req_buffer *rfnm_usb_req_buffer_out;
struct rfnm_usb_req_buffer *rfnm_usb_req_buffer_out_usb;

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
};

struct rfnm_dev {
	struct rfnm_rx_usb_cb rx_usb_cb;
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


static void rfnm_usb_buffer_done_in(struct usb_ep_queue_ele *usb_ep_queue_ele)
{
	unsigned long flags;
	int status;

	spin_lock_irqsave(&rfnm_usb_req_buffer_in->list_lock, flags);
	list_del(&usb_ep_queue_ele->head);
	spin_unlock_irqrestore(&rfnm_usb_req_buffer_in->list_lock, flags);

	status = usb_ep_queue(usb_ep_queue_ele->ep, usb_ep_queue_ele->req, GFP_ATOMIC);
	if (status) {
		printk("kill %s:  resubmit %d bytes --> %d\n",usb_ep_queue_ele->ep->name, usb_ep_queue_ele->req->length, status);
		usb_ep_set_halt(usb_ep_queue_ele->ep);
		// FIXME recover later ... somehow 
	}

	kfree(usb_ep_queue_ele);
}


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
		usb_ep_set_halt(usb_ep_queue_ele->ep);
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

	spin_lock(&rfnm_usb_req_buffer_in->list_lock);
	usb_ep_queue_ele = list_first_entry_or_null(&rfnm_usb_req_buffer_in->active, struct usb_ep_queue_ele, head);
	spin_unlock(&rfnm_usb_req_buffer_in->list_lock);

	return usb_ep_queue_ele != NULL;
}

int can_run_handler_usb(void) {
	struct usb_ep_queue_ele *usb_ep_queue_ele_in;

	spin_lock(&rfnm_usb_req_buffer_in_usb->list_lock);
	usb_ep_queue_ele_in = list_first_entry_or_null(&rfnm_usb_req_buffer_in_usb->active, struct usb_ep_queue_ele, head);
	spin_unlock(&rfnm_usb_req_buffer_in_usb->list_lock);

	struct usb_ep_queue_ele *usb_ep_queue_ele_out;

	spin_lock(&rfnm_usb_req_buffer_out_usb->list_lock);
	usb_ep_queue_ele_out = list_first_entry_or_null(&rfnm_usb_req_buffer_out_usb->active, struct usb_ep_queue_ele, head);
	spin_unlock(&rfnm_usb_req_buffer_out_usb->list_lock);
	
	return (usb_ep_queue_ele_in != NULL) || (usb_ep_queue_ele_out != NULL) || rfnm_dev->usb_flushmode || rfnm_dev->wq_stop_usb;
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
		wait_event(wq_usb, can_run_handler_usb());
		if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_5);

		//if(rfnm_dev->wq_stop_usb) {
		//	rfnm_dev->usb_flushmode = 1;
		//	printk("Stopping USB thread by entering flushmode");
		//}

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
						usb_ep_set_halt(usb_ep_queue_ele->ep);
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
			printk("Stopping USB process");
			do_exit(0);
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

		dcache_clean_poc(usb_ep_queue_ele->req->buf, usb_ep_queue_ele->req->buf + usb_ep_queue_ele->req->length);

		//printk("DQ %lx\n", usb_ep_queue_ele->req->buf);


#if 0

		static int delay_wavedetct = 0;
		static int wavedetect = 0;

		if(1 || ++delay_wavedetct == 10000) {
			delay_wavedetct = 0;

			for (int s = 0; s < 50000; s += 768) {

				uint32_t *packed;
				uint32_t lp;
				int16_t li, lq;

				packed = (uint32_t *) (((uint8_t*) usb_ep_queue_ele->req->buf) + 32 + s);
				lp = *packed;

				li = ((lp & 0xfff000ll) >> 12) << 4;
				lq = (lp & 0xfff) << 4;

				li = abs(li);
				lq = abs(lq);
				

#if 1
				if((li + lq) > (150 * 4)) {
					if(!wavedetect) {
						wavedetect = 1;
						printk("Y %d %d \t%d\n", lq, li, s);
					}
					
				} else if((li + lq) < (20 * 4)){
					if(wavedetect) {
						wavedetect = 0;
						printk("N %d %d \t\t%d\n", lq, li, s);
					}
				}
#else
				printk("%d %d\n", li, lq);
#endif
			}
		}
#endif

#if 0
	struct rfnm_rx_usb_buf *rfnm_rx_usb_buf_t;

	rfnm_rx_usb_buf_t = usb_ep_queue_ele->req->buf;

	static int d_dbg = 0;
				if(++d_dbg == 4) {
					d_dbg = 0;
					printk("Q %ld\n", rfnm_rx_usb_buf_t->usb_cc);
				}
#endif
		//memset(((uint8_t*) usb_ep_queue_ele->req->buf) + 32, 0xee, 16);

		status = usb_ep_queue(usb_ep_queue_ele->ep, usb_ep_queue_ele->req, GFP_ATOMIC);
		if (status) {
			printk("kill %s:  resubmit %d bytes --> %d\n",usb_ep_queue_ele->ep->name, usb_ep_queue_ele->req->length, status);
			usb_ep_set_halt(usb_ep_queue_ele->ep);
			// FIXME recover later ... somehow 
		}

		if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_6);
		if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_6);
		kfree(usb_ep_queue_ele);

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
			usb_ep_set_halt(usb_ep_queue_ele->ep);
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




#if 0
struct work_struct  rfnm_tasklet_usb;
DECLARE_WORK(rfnm_tasklet_usb, rfnm_handler_usb);
#else
//static void rfnm_handler_usb(unsigned long tasklet_data);
//static DECLARE_TASKLET_OLD(rfnm_tasklet_usb, &rfnm_handler_usb);
#endif
//static void rfnm_tasklet_handler_in(unsigned long tasklet_data) {
int rfnm_handler_in(void * tasklet_data) {
//void rfnm_handler_in(struct work_struct * tasklet_data) {


struct sched_param sparam = { .sched_priority = 1 };
sched_setscheduler(current, SCHED_FIFO, &sparam);

	
//kernel_neon_begin();

while(1) {

	//wait_event(wq_in, can_run_handler_in());


	//might_sleep();
	//__wait_event(wq_in, can_run_handler_in());	
    
	//sleep_on(&wq_in);

	if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_1);

	*gpio4 = *gpio4 | (0x1 << 4);

	

	struct usb_ep_queue_ele *usb_ep_queue_ele;
	
//tasklet_again:

	uint8_t *dcache;

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

		if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_1);
		usleep_range(500, 1000);
		if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_1);
		
		//schedule();
		//reinit_completion(&setup_done); wait_for_completion(&setup_done);	

		

		goto exit_tasklet;
	}

	la_readable -= 16;

	if(la_readable < 0) {
		la_readable = 0;
		
		if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_1);
		usleep_range(500, 1000);
		if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_1);
		
		//schedule();
		//reinit_completion(&setup_done); wait_for_completion(&setup_done);	
		goto exit_tasklet;
	}

	if(la_readable > (RFNM_ADC_BUFCNT / 4)) {
		// too many buffers behind, log error and jump forward
		rfnm_dev->rx_la_cb.tail = rfnm_m7_status->rx_head;
		printk("rx too many buffers behind, error not logged to buffer...\n");
		
		if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_1);
		usleep_range(500, 1000);
		if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_1);
		
		//schedule();
		//reinit_completion(&setup_done); wait_for_completion(&setup_done);	
		goto exit_tasklet;
	}

	if(la_readable) {
	//	printk("readable %d head %d tail %d\n", la_readable, la_head, la_tail);
	}

	*gpio4 = *gpio4 | (0x1 << 7);


	if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_2);
	if(la_tail + la_readable >= RFNM_ADC_BUFCNT) {
		dcache_inval_poc((unsigned char *) &rfnm_bufdesc_rx[la_tail], (unsigned char *) &rfnm_bufdesc_rx[RFNM_ADC_BUFCNT/* - 1*/]);
		dcache_inval_poc((unsigned char *) &rfnm_bufdesc_rx[0], (unsigned char *) &rfnm_bufdesc_rx[la_tail + la_readable + 1 - RFNM_ADC_BUFCNT]);
		//printk("invalid %d to %d and %d to %d\n", la_tail, RFNM_ADC_BUFCNT, 0, la_tail + la_readable + 1 - RFNM_ADC_BUFCNT);
	} else {
		dcache_inval_poc((unsigned char *) &rfnm_bufdesc_rx[la_tail], (unsigned char *) &rfnm_bufdesc_rx[la_tail + la_readable + 1]);
		//printk("invalid %d to %d \n", la_tail, la_tail + la_readable + 1);
	}
	if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_2);
	
	//dcache = (unsigned char *) &rfnm_bufdesc_rx[la_tail];
	//dcache_inval_poc(dcache, dcache + SZ_64K /*sizeof(struct rfnm_bufdesc_rx)*/);
	*gpio4 = *gpio4 & ~(0x1 << 7);

	barrier();
	
 	
	*gpio4 = *gpio4 | (0x1 << 6);

	//kernel_neon_begin();

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
			

			struct usb_ep_queue_ele *usb_ep_queue_ele;
			
			
			#if 0
			spin_lock(&rfnm_usb_req_buffer_in->list_lock);
			usb_ep_queue_ele = list_first_entry_or_null(&rfnm_usb_req_buffer_in->active, struct usb_ep_queue_ele, head);
			spin_unlock(&rfnm_usb_req_buffer_in->list_lock);

			if(usb_ep_queue_ele == NULL) {
				*gpio4 = *gpio4 | (0x1 << 8); *gpio4 = *gpio4 & ~(0x1 << 8);
				rfnm_stream_stats.usb_rx_error[0]++;
			} else {
				usb_ep_queue_ele->req->buf = (uint8_t *) &rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]];
				usb_ep_queue_ele->req->length = sizeof(struct rfnm_rx_usb_buf);
				kernel_neon_end();
				rfnm_usb_buffer_done_in(usb_ep_queue_ele);
				kernel_neon_begin();
				rfnm_stream_stats.usb_rx_ok[0]++;
			}
			#else
			spin_lock(&rfnm_usb_req_buffer_in->list_lock);
			usb_ep_queue_ele = list_first_entry_or_null(&rfnm_usb_req_buffer_in->active, struct usb_ep_queue_ele, head);
			spin_unlock(&rfnm_usb_req_buffer_in->list_lock);

			if(usb_ep_queue_ele == NULL) {
				*gpio4 = *gpio4 | (0x1 << 8); *gpio4 = *gpio4 & ~(0x1 << 8);
				rfnm_stream_stats.usb_rx_error[0]++;
			} else {
				usb_ep_queue_ele->req->buf = (uint8_t *) &rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]];
				usb_ep_queue_ele->req->length = sizeof(struct rfnm_rx_usb_buf);

				//printk("scheduling\n");


				//printk("Q %lx\n", usb_ep_queue_ele->req->buf);
				
					


#if 0

		static int delay_wavedetct = 0;
		static int wavedetect = 0;

		if(1 || ++delay_wavedetct == 10000) {
			delay_wavedetct = 0;

			for (int s = 0; s < 50000; s += 768) {

				uint32_t *packed;
				uint32_t lp;
				int16_t li, lq;

				packed = (uint32_t *) (((uint8_t*) usb_ep_queue_ele->req->buf) + 32 + s);
				lp = *packed;

				li = ((lp & 0xfff000ll) >> 12) << 4;
				lq = (lp & 0xfff) << 4;

				li = abs(li);
				lq = abs(lq);
				

#if 1
				if((li + lq) > (150 * 4)) {
					if(!wavedetect) {
						wavedetect = 1;
						printk("Q Y %d %d \t%d\n", lq, li, s);
					}
					
				} else if((li + lq) < (20 * 4)){
					if(wavedetect) {
						wavedetect = 0;
						printk("Q N %d %d \t\t%d\n", lq, li, s);
					}
				}
#else
				printk("%d %d\n", li, lq);
#endif
			}
		}
#endif

				


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

				
//kernel_neon_end();
				wake_up(&wq_usb);
				//tasklet_schedule(&rfnm_tasklet_usb);
				//schedule_work(&rfnm_tasklet_usb);
//kernel_neon_begin();

				rfnm_stream_stats.usb_rx_ok[0]++;
			}
			#endif
			

			//spin_lock(&rfnm_dev->rx_usb_cb.reader_lock);
			
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

		static int delay_wavedetct = 0;
		static int wavedetect = 0;

		if(0 || delay_wavedetct == 10000) {
			delay_wavedetct = 0;


			int16_t *q, *i;
			uint32_t *t;

			q = (int16_t *) &rfnm_bufdesc_rx[la_tail].buf[0];
			t = (uint32_t *) &rfnm_bufdesc_rx[la_tail].buf[0];

			int16_t li, lq;

			li = (0xffff0000 & (*t)) >> 16;
			lq = *q;

			li = abs(li);
			lq = abs(lq);

#if 0
			if((li + lq) > 150) {
				if(!wavedetect) {
					wavedetect = 1;
					printk("yes %d\t%d %d\n", la_tail, lq, li);
				}
				
			} else if((li + lq) < 20){
				if(wavedetect) {
					wavedetect = 0;
					printk("no %d\t%d %d\n", la_tail, lq, li);
				}
			}
#else
			printk("%d %d\n", li, lq);
#endif
		}
#endif

#if 1
		//if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_4);
		kernel_neon_begin();
		rfnm_pack16to12_aarch64( (uint8_t *) &rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].buf[LA_RX_BASE_BUFSIZE_12 * rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id]], 
					(uint8_t *) rfnm_bufdesc_rx[la_tail].buf, 
					LA_RX_BASE_BUFSIZE / 1);
		kernel_neon_end();
		//if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_4);	

#endif

#if 0
	if(rfnm_dev->rx_usb_cb.adc_buf[la_adc_id] == 100)
	printk("%d %d %d\n", rfnm_dev->rx_usb_cb.adc_buf[la_adc_id], rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id], rfnm_bufdesc_rx[la_tail].cc);
#endif


#if 0

		static int delay_wavedetct = 0;
		static int wavedetect = 0;

		if(1 || ++delay_wavedetct == 10000) {
			delay_wavedetct = 0;

			uint32_t *packed;
			uint32_t lp;
			int16_t li, lq;

			packed = (uint32_t *) &rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].buf[LA_RX_BASE_BUFSIZE_12 * rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id]];
			lp = *packed;

			li = ((lp & 0xfff000ll) >> 12) << 4;
			lq = (lp & 0xfff) << 4;

			li = abs(li);
			lq = abs(lq);
			

#if 1
			if((li + lq) > (150 * 4)) {
				if(!wavedetect) {
					wavedetect = 1;
					printk("yes %d\t%d %d\n", la_tail, lq, li);
				}
				
			} else if((li + lq) < (20 * 4)){
				if(wavedetect) {
					wavedetect = 0;
					printk("no %d\t%d %d\n", la_tail, lq, li);
				}
			}
#else
			printk("%d %d\n", li, lq);
#endif
		}
#endif
//delay_wavedetct++;


		if(!rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id]) {
			rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].magic = 0x7ab8bd6f;
			rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].phytimer = rfnm_bufdesc_rx[la_tail].phytimer;
			rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].usb_cc = ++rfnm_dev->rx_usb_cb.usb_cc[la_adc_id];
			rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].adc_id = la_adc_id;
			rfnm_rx_usb_buf[rfnm_dev->rx_usb_cb.adc_buf[la_adc_id]].adc_cc = la_adc_cc;
		}


			
		
		if(rfnm_dev->rx_la_cb.adc_cc[la_adc_id] != la_adc_cc) {
#if 0
			printk("cc mismatch on adc %d -> %d vs %d tail is %d axiq is %d | adc_buf_cnt %d adc_buf %d head %d\n", la_adc_id, 
				la_adc_cc, rfnm_dev->rx_la_cb.adc_cc[la_adc_id], 
				la_tail, rfnm_bufdesc_rx[la_tail].axiq_done,
				rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id], rfnm_dev->rx_usb_cb.adc_buf[la_adc_id], rfnm_dev->rx_usb_cb.head);
#endif
			rfnm_dev->rx_la_cb.adc_cc[la_adc_id] = la_adc_cc;

			rfnm_stream_stats.la_adc_error[la_adc_id]++;
		} else {
			rfnm_stream_stats.la_adc_ok[la_adc_id]++;
		}
		//la_adc_cc++;
		rfnm_dev->rx_la_cb.adc_cc[la_adc_id]++;

		if(++la_tail == RFNM_ADC_BUFCNT) {
			la_tail = 0;
		}

		rfnm_dev->rx_usb_cb.adc_buf_cnt[la_adc_id]++;
	}

	//kernel_neon_end();

	*gpio4 = *gpio4 & ~(0x1 << 6);
	

	rfnm_dev->rx_la_cb.tail = la_tail;

	rfnm_m7_status->kernel_cache_flush_tail = la_tail;

	
	

	//printk("head is at %d\n", rfnm_m7_status->rx_head);

exit_tasklet: 
	//spin_unlock(&rfnm_dev->rx_usb_cb.reader_lock);
exit_tasklet_no_unlock:
	
	//kernel_neon_end();
	*gpio4 = *gpio4 & ~(0x1 << 4);

	
	
//	tasklet_hi_schedule(&rfnm_tasklet_in);
	//schedule_work(&rfnm_tasklet_in);

	if(rfnm_dev->wq_stop_in) {
		printk("stopping IN process\n");
		rfnm_dev->wq_stop_in = 0;
		do_exit(0);
	}

}
}


int can_run_handler_out(void) {
	struct usb_ep_queue_ele *usb_ep_queue_ele;

	spin_lock(&rfnm_usb_req_buffer_out->list_lock);
	usb_ep_queue_ele = list_first_entry_or_null(&rfnm_usb_req_buffer_out->active, struct usb_ep_queue_ele, head);
	spin_unlock(&rfnm_usb_req_buffer_out->list_lock);

	return usb_ep_queue_ele != NULL;
}

static int rfnm_order_tx_usb_buf(void *priv, const struct list_head *a, const struct list_head *b) {
	const struct usb_ep_queue_ele *bcm_a = list_entry(a, struct usb_ep_queue_ele, head);
	const struct usb_ep_queue_ele *bcm_b = list_entry(b, struct usb_ep_queue_ele, head);
	

	return bcm_a->usb_cc - bcm_b->usb_cc;
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


	while(1) {

	//	wait_event(wq_out, can_run_handler_out());


		struct usb_ep_queue_ele *usb_ep_queue_ele;
again:
		uint32_t list_size = 0;
		int cc_is_continuous = 0;

		spin_lock(&rfnm_usb_req_buffer_out->list_lock);
		list_sort(NULL, &rfnm_usb_req_buffer_out->active, rfnm_order_tx_usb_buf);
		usb_ep_queue_ele = list_first_entry_or_null(&rfnm_usb_req_buffer_out->active, struct usb_ep_queue_ele, head);
		
		list_size = list_count_nodes(&rfnm_usb_req_buffer_out->active);
		spin_unlock(&rfnm_usb_req_buffer_out->list_lock);

		

		if(usb_ep_queue_ele != NULL) {
			struct rfnm_tx_usb_buf *lb = usb_ep_queue_ele->req->buf;
			// incredible mistery:
			// not having this list_size requirement 
			// increases the number of cc errors...?????
			if(list_size > 2 && lb->usb_cc == rfnm_dev->tx_la_cb.usb_cc) {
				cc_is_continuous = 1;
			}
		}

		if( (list_size > 8 && usb_ep_queue_ele != NULL) || (cc_is_continuous) ) {

			uint32_t la_tail = rfnm_m7_status->tx_buf_id;
			uint32_t la_head = rfnm_dev->tx_la_cb.head;

			barrier();

			uint32_t la_writable, la_margin;
			
			if(la_tail < la_head) {
				la_writable = RFNM_DAC_BUFCNT - la_head + la_tail;
			} else {
				la_writable = la_tail - la_head;
			}

			la_margin = RFNM_DAC_BUFCNT - la_writable;
/*
[ 1881.458194] tx too many buffers behind ... tail 1001 head 8507 writable (8878)
[ 1882.073539] tx too many buffers behind ... tail 9306 head 12265 writable (13425)
[ 1882.467415] tx too many buffers behind ... tail 7419 head 15578 writable (8225)
[ 1882.646557] tx too many buffers behind ... tail 12531 head 3835 writable (8696)
[ 1883.534262] tx too many buffers behind ... tail 4367 head 12531 writable (8220)
[ 1883.534281] tx too many buffers behind ... tail 4370 head -16385 writable (37139)
[ 1883.534285] tx too many buffers behind ... tail 4370 head -16385 writable (37139)
[ 1883.534289] tx too many buffers behind ... tail 4371 head -16385 writable (37140)

rfnm_tx_la_cb

*/

			int max_latency = 4500;


			if(la_margin < 20) {
			//if(la_writable < 20 || la_writable > (RFNM_DAC_BUFCNT - 20)) {
				// too many buffers behind, log error and jump forward
				rfnm_dev->tx_la_cb.head = rfnm_m7_status->tx_buf_id + (max_latency/3);
				if(rfnm_dev->tx_la_cb.head >= RFNM_DAC_BUFCNT) {
					rfnm_dev->tx_la_cb.head -= RFNM_DAC_BUFCNT;
				}

				if(rfnm_dev->tx_la_cb.head < 0) {
					printk("why is head < 0 lol was %d\n",  rfnm_m7_status->tx_buf_id); 
					while(1) {mdelay(1);}
				}

				printk("tx too many buffers behind ... tail %d head %d writable (%d) new head %d txid %d\n", 
					la_tail, la_head, la_writable, rfnm_dev->tx_la_cb.head, rfnm_m7_status->tx_buf_id);
				rfnm_stream_stats.usb_tx_error[0]++;
				//rfnm_stream_stats.la_dac_error[0]++;
				continue;
				//usleep_range(500, 1000);
				//goto exit_tasklet;
			}

			

			if(la_margin > (max_latency)) {
				// too many buffers behind, log error and jump forward
				rfnm_dev->tx_la_cb.head = rfnm_m7_status->tx_buf_id + (max_latency/3);
				if(rfnm_dev->tx_la_cb.head >= RFNM_DAC_BUFCNT) {
					rfnm_dev->tx_la_cb.head -= RFNM_DAC_BUFCNT;
				}

				printk("reducing tx latency ... tail %d head %d writable (%d) new head %d txid %d\n", 
					la_tail, la_head, la_writable, rfnm_dev->tx_la_cb.head, rfnm_m7_status->tx_buf_id);
				rfnm_stream_stats.usb_tx_error[0]++;
				continue;
			}

			if(la_writable < RFNM_TX_USB_BUF_MULTI) {
				continue;
			}

			if(la_writable > RFNM_TX_USB_BUF_MULTI) {
				la_writable = RFNM_TX_USB_BUF_MULTI;
			}

			dcache_inval_poc(usb_ep_queue_ele->req->buf, usb_ep_queue_ele->req->buf + usb_ep_queue_ele->req->length);
			
			struct rfnm_tx_usb_buf *lb = usb_ep_queue_ele->req->buf;


			if(lb->usb_cc != rfnm_dev->tx_la_cb.usb_cc) {
				printk("usb cc error %d vs %d .. tail %d head %d writable (%d) list %d\n", lb->usb_cc, rfnm_dev->tx_la_cb.usb_cc, la_tail, la_head, la_writable, list_size);
				rfnm_stream_stats.usb_tx_error[0]++;
				rfnm_dev->tx_la_cb.usb_cc = lb->usb_cc;
			}

			rfnm_dev->tx_la_cb.usb_cc++;



			

			//rfnm_stream_stats.la_dac_ok[0]++;

				//printk("tail %d head %d writable (%d)\n", la_tail, la_head, la_writable);

			


			//printk("magic is %x\n", lb->magic); 0x758f4d4a

			for(int w = 0; w < la_writable; w++) {
				//
				// LA_TX_BASE_BUFSIZE_12 * RFNM_TX_USB_BUF_MULTI
#if 1
				kernel_neon_begin();
				rfnm_unpack12to16_aarch64( 
							(uint8_t *) rfnm_bufdesc_tx[rfnm_dev->tx_la_cb.head].buf,
							(uint8_t *) &lb->buf[ w * LA_TX_BASE_BUFSIZE_12 ],
							LA_TX_BASE_BUFSIZE);
				kernel_neon_end();
#endif

#if 0
				memcpy( 
							(uint8_t *) rfnm_bufdesc_tx[rfnm_dev->tx_la_cb.head].buf,
							(uint8_t *) &lb->buf[ w * LA_TX_BASE_BUFSIZE_12 ],
							LA_TX_BASE_BUFSIZE);
#endif
				rfnm_bufdesc_tx[rfnm_dev->tx_la_cb.head].cc = rfnm_dev->tx_la_cb.dac_cc++;

				//printk("%lx %lx -> %lx\n", (uint8_t *) rfnm_bufdesc_tx[rfnm_dev->tx_la_cb.head].buf,
				//			(uint8_t *) &lb->buf[ w * LA_TX_BASE_BUFSIZE_12 ], lb->buf[ w * LA_TX_BASE_BUFSIZE_12 ]);

				if(++rfnm_dev->tx_la_cb.head == RFNM_DAC_BUFCNT) {
					rfnm_dev->tx_la_cb.head = 0;
				}

			}


			if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_4);
			if(la_head + la_writable >= RFNM_DAC_BUFCNT) {
				dcache_clean_poc((unsigned char *) &rfnm_bufdesc_tx[la_head], (unsigned char *) &rfnm_bufdesc_tx[RFNM_DAC_BUFCNT/* - 1*/]);
				dcache_clean_poc((unsigned char *) &rfnm_bufdesc_tx[0], (unsigned char *) &rfnm_bufdesc_tx[la_head + la_writable - RFNM_DAC_BUFCNT + 1]);
			} else {
				dcache_clean_poc((unsigned char *) &rfnm_bufdesc_tx[la_head], (unsigned char *) &rfnm_bufdesc_tx[la_head + la_writable + 1]);
			}
			if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_4);
			

			







#if 1
			struct usb_ep_queue_ele usb_ep_queue_ele_tmp;

			memcpy(&usb_ep_queue_ele_tmp, usb_ep_queue_ele, sizeof(struct usb_ep_queue_ele));

			spin_lock(&rfnm_usb_req_buffer_out_usb->list_lock);
			spin_lock(&rfnm_usb_req_buffer_out->list_lock);

			list_add_tail(&usb_ep_queue_ele->head, &rfnm_usb_req_buffer_out_usb->active);
			
			list_del(&usb_ep_queue_ele_tmp.head);

			spin_unlock(&rfnm_usb_req_buffer_out_usb->list_lock);
			spin_unlock(&rfnm_usb_req_buffer_out->list_lock);

			wake_up(&wq_usb);
#else		
			rfnm_usb_buffer_done_out(usb_ep_queue_ele);
#endif

			rfnm_stream_stats.usb_tx_ok[0]++;
			goto again;
		}

		if(rfnm_dev->wq_stop_out) {
			printk("stopping OUT process\n");
			rfnm_dev->wq_stop_out = 0;
			do_exit(0);
		}

		
		
		
		if(GPIO_DEBUG) rfnm_gpio_clear(0, RFNM_DGB_GPIO4_3);
		usleep_range(500, 1000);
		if(GPIO_DEBUG) rfnm_gpio_set(0, RFNM_DGB_GPIO4_3);
	}
		
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

int rfnm_callback_init(struct la9310_dev *la9310_dev)
{
	int ret = 0;
	dev_info(la9310_dev->dev, "RFNM Callback registered\n");
	ret = register_rfnm_callback((void *)callback_func_0, 0);
	ret = register_rfnm_callback((void *)callback_func_1, 1);
	//last_print_time = ktime_get();

	return ret;
}

int unregister_rfnm_callback(void );
int rfnm_callback_deinit(void)
{
	int ret = 0;
	ret = unregister_rfnm_callback();
	return ret;
}





static void rfnm_submit_usb_req_in(struct usb_ep *ep, struct usb_request *req)
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
		//free_ep_req(ep, req);
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
#if 1
	struct usb_ep_queue_ele *new_ele;

	new_ele = kzalloc(sizeof(struct usb_ep_queue_ele), GFP_KERNEL);
	new_ele->ep = ep;
	new_ele->req = req;
	//new_ele->dwc_queue_sent = 0;

	unsigned long flags;

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
		usb_ep_set_halt(ep);
		// FIXME recover later ... somehow 
	}

#endif

	// actual was working before... what changed?
	rfnm_stream_stats.usb_rx_bytes[0] += req->actual;





	/*status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		printk("kill %s:  resubmit %d bytes --> %d\n", ep->name, req->length, status);
		usb_ep_set_halt(ep);
		// FIXME recover later ... somehow 
	}*/
}

EXPORT_SYMBOL_GPL(rfnm_submit_usb_req_in);








static void rfnm_submit_usb_req_out(struct usb_ep *ep, struct usb_request *req)
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
#if 1
	struct usb_ep_queue_ele *new_ele;
	struct rfnm_tx_usb_buf *lb = req->buf;

	new_ele = kzalloc(sizeof(struct usb_ep_queue_ele), GFP_KERNEL);
	new_ele->ep = ep;
	new_ele->req = req;
	new_ele->usb_cc = lb->usb_cc;
	//new_ele->dwc_queue_sent = 0;

	unsigned long flags;

	spin_lock_irqsave(&rfnm_usb_req_buffer_out->list_lock, flags);
	list_add_tail(&new_ele->head, &rfnm_usb_req_buffer_out->active);
	spin_unlock_irqrestore(&rfnm_usb_req_buffer_out->list_lock, flags);


	//wake_up(&wq_out);
	//tasklet_schedule(&rfnm_tasklet_out);
	//schedule_work(&rfnm_tasklet_out);

	// actual was working before... what changed?
	rfnm_stream_stats.usb_tx_bytes[0] += req->actual;
#else

	// actual was working before... what changed?
	rfnm_stream_stats.usb_tx_bytes[0] += req->actual;

	status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		printk("kill %s:  resubmit %d bytes --> %d\n", ep->name, req->length, status);
		usb_ep_set_halt(ep);
		// FIXME recover later ... somehow 
	}

#endif
	






	/*status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		printk("kill %s:  resubmit %d bytes --> %d\n", ep->name, req->length, status);
		usb_ep_set_halt(ep);
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
	memcpy(&r_stat->stream_stats, &rfnm_stream_stats, sizeof(struct rfnm_stream_stats));

	//memcpy(&r_stat->m7_status, (uint8_t *) rfnm_m7_status, sizeof(struct rfnm_m7_status));	
	// kazan freezes during this memcpy -- just copy it over manually

	r_stat->m7_status.tx_buf_id = rfnm_m7_status->tx_buf_id;
	r_stat->m7_status.rx_head = rfnm_m7_status->rx_head;
	r_stat->m7_status.kernel_cache_flush_tail = rfnm_m7_status->kernel_cache_flush_tail;
	
	// cc is advanced by an extra element from the main loop
	if(rfnm_dev->tx_la_cb.usb_cc > 0) {
		r_stat->usb_dac_last_dqbuf = rfnm_dev->tx_la_cb.usb_cc - 1;
	} else {
		r_stat->usb_dac_last_dqbuf = 0;
	}
	
}
EXPORT_SYMBOL(rfnm_populate_dev_status);




static ssize_t dfs_rfnm_stream_status_read(struct file *f, char *buffer, size_t len, loff_t *offset)
{
	char data[1000];
	int data_len = 0;

	uint64_t time_diff, time_processing_start;
	static uint64_t last_print_time = 0;
	static struct rfnm_stream_stats last_stats;

	
	time_processing_start = ktime_get();
	time_diff = time_processing_start - last_print_time;
	last_print_time = time_processing_start;

	


	data_len += sprintf(&data[data_len], "usb rx ok:\t\t%ld\t%ld\n", rfnm_stream_stats.usb_rx_ok[0], rfnm_stream_stats.usb_rx_ok[1]);
	data_len += sprintf(&data[data_len], "usb rx error:\t%ld\t%ld\n", rfnm_stream_stats.usb_rx_error[0], rfnm_stream_stats.usb_rx_error[1]);

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

	data_len += sprintf(&data[data_len], "\n");

	data_len += sprintf(&data[data_len], "dac ok:\t\t%ld\n", rfnm_stream_stats.la_dac_ok[0]);
	data_len += sprintf(&data[data_len], "dac error:\t%ld\n", rfnm_stream_stats.la_dac_error[0]);






	data_len += sprintf(&data[data_len], "\n");

	data_len += sprintf(&data[data_len], "\t\thead\ttail\treadable\t\n");



	uint32_t la_tail = rfnm_m7_status->tx_buf_id;
	uint32_t la_head = rfnm_dev->tx_la_cb.head;
	uint32_t la_writable, la_margin;  
	
	if(la_tail < la_head) {
		la_writable = RFNM_DAC_BUFCNT - la_head + la_tail;
	} else {
		la_writable = la_tail - la_head;
	}

	la_margin = RFNM_DAC_BUFCNT - la_writable;

	data_len += sprintf(&data[data_len], "writer:\t\t%d\t%d\t%d\n", la_head, la_tail, la_margin);


	la_head = rfnm_m7_status->rx_head;
	la_tail = rfnm_dev->rx_la_cb.tail;

	uint32_t la_readable = la_head - la_tail;

	if(la_head < la_tail) {
		la_readable += RFNM_ADC_BUFCNT;
	}

	data_len += sprintf(&data[data_len], "reader:\t\t%d\t%d\t%d\n", la_head, la_tail, la_readable);

	data_len += sprintf(&data[data_len], "\n");


	uint32_t ls_in, ls_in_usb, ls_out, ls_out_usb;
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
	

	data_len += sprintf(&data[data_len], "ls in:\t\t%d\n", ls_in);
	data_len += sprintf(&data[data_len], "ls in usb:\t%d\n", ls_in_usb);
	data_len += sprintf(&data[data_len], "ls out:\t\t%d\n", ls_out);
	data_len += sprintf(&data[data_len], "ls out usb:\t%d\n", ls_out_usb);

	


	memcpy(&last_stats, &rfnm_stream_stats, sizeof(struct rfnm_stream_stats));

	return simple_read_from_buffer(buffer, len, offset, data, data_len);
}

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


const struct file_operations dfs_rfnm_stream_fops = {
	.owner = THIS_MODULE,
	.read = dfs_rfnm_stream_status_read,
};


static struct dentry *dfs_rfnm_dir;
static struct dentry *dfs_rfnm_stream_stat;

void stop_sm(void) {
	
	rfnm_dev->wq_stop_in = 1;
	rfnm_dev->wq_stop_out = 1;
	while(rfnm_dev->wq_stop_in || rfnm_dev->wq_stop_out) { mdelay(1); }
	rfnm_dev->wq_stop_usb = 1;
	wake_up(&wq_usb);
	while(rfnm_dev->wq_stop_usb) { mdelay(1); }
}

void start_sm(void) {
	
	kthread_run_on_cpu(rfnm_handler_in, NULL, 1, "RX");
	kthread_run_on_cpu(rfnm_handler_out, NULL, 2, "TX");
	kthread_run_on_cpu(rfnm_handler_usb, NULL, 3, "USB");
}

void rfnm_reset_sm(void) {

	int i;

	rfnm_dev->rx_usb_cb.head = 0;
	rfnm_dev->rx_usb_cb.cc = 0;
	
	rfnm_dev->rx_usb_cb.usb_host_dropped = 0;
	rfnm_dev->rx_la_cb.tail = 0;
	
	for(i = 0; i < 4; i++) {
		rfnm_dev->rx_usb_cb.adc_buf[i] = 0;
		rfnm_dev->rx_usb_cb.adc_buf_cnt[i] = RFNM_RX_USB_BUF_MULTI;
		rfnm_dev->rx_la_cb.adc_cc[i] = 0;
		rfnm_dev->rx_usb_cb.usb_cc[i] = 0;
	}

	rfnm_dev->tx_la_cb.head = 0;
	rfnm_dev->tx_la_cb.dac_cc = 0;
	rfnm_dev->tx_la_cb.usb_cc = 0;

	rfnm_dev->wq_stop_in = 0;
	rfnm_dev->wq_stop_out = 0;
	rfnm_dev->wq_stop_usb = 0;

	memset(&rfnm_stream_stats, 0, sizeof(struct rfnm_stream_stats));
}


void rfnm_restart_sm(int hard) {

	int i;

	if(hard) {
		stop_sm();
	} else {
		rfnm_dev->usb_flushmode = 1;

		wake_up(&wq_usb);

		//if(wait) {
			while(rfnm_dev->usb_flushmode) {
				mdelay(1);
			}
		//}
	}

	rfnm_reset_sm();

	if(hard) {
		start_sm();
	}

}
EXPORT_SYMBOL(rfnm_restart_sm);



static int __init la9310_rfnm_init(void)
{
	int err = 0, i;
	struct la9310_dev *la9310_dev;

	init_completion(&setup_done);
		
	//hrtimer_init(&test_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	//test_hrtimer.function = &test_hrtimer_handler;
	//hrtimer_start(&test_hrtimer, ms_to_ktime(1), HRTIMER_MODE_REL);

	rfnm_dev = kzalloc(sizeof(struct rfnm_dev), GFP_KERNEL);

	dfs_rfnm_dir = debugfs_create_dir("rfnm", NULL);
	dfs_rfnm_stream_stat = debugfs_create_file("stream_status", 0644, dfs_rfnm_dir, NULL, &dfs_rfnm_stream_fops);



	rfnm_dev->usb_config_buffer = kzalloc(CONFIG_DESCRIPTOR_MAX_SIZE, GFP_KERNEL);
	

	la9310_dev = get_la9310_dev_byname("nlm0");
	if (la9310_dev == NULL) {
		pr_err("No LA9310 device named nlm0\n");
		return -ENODEV;
	}

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

	//dev_info(la9310_dev->dev, "Mapped IQflood from %x to %p\n", RFNM_IQFLOOD_MEMADDR, rfnm_iqflood_vmem);


	gpio4_iomem = ioremap(0x30230000, SZ_4K);
	gpio4 = (volatile unsigned int *) gpio4_iomem;
	gpio4_initial = *gpio4;

	// disable gpio4
	gpio4 = kzalloc(SZ_4K, GFP_KERNEL);

	rfnm_bufdesc_rx = (struct rfnm_bufdesc_rx *) memremap(0x96400000, SZ_64M, MEMREMAP_WB);
	dev_info(la9310_dev->dev, "Mapped rfnm_bufdesc_rx from %x to %lx size %d\n", 0x96400000, rfnm_bufdesc_rx, (sizeof(struct rfnm_bufdesc_rx) * RFNM_ADC_BUFCNT));

	rfnm_bufdesc_tx = (struct rfnm_bufdesc_tx *) memremap(0x96400000 + (sizeof(struct rfnm_bufdesc_rx) * RFNM_ADC_BUFCNT), SZ_64M, MEMREMAP_WB);
	dev_info(la9310_dev->dev, "Mapped rfnm_bufdesc_tx from %x to %lx size %d\n", 0x96400000 + (sizeof(struct rfnm_bufdesc_rx) * RFNM_ADC_BUFCNT), rfnm_bufdesc_tx, (sizeof(struct rfnm_bufdesc_tx) * RFNM_DAC_BUFCNT));

	rfnm_rx_usb_buf = (struct rfnm_rx_usb_buf *) memremap(
						0x96400000 +  (sizeof(struct rfnm_bufdesc_rx) * RFNM_ADC_BUFCNT) + SZ_64M, 
						sizeof(struct rfnm_rx_usb_buf) * RFNM_RX_USB_BUF_SIZE, MEMREMAP_WB);

	dev_info(la9310_dev->dev, "Mapped rfnm_rx_usb_buf from %x to %lx size %d\n", 
			0x96400000 +  (sizeof(struct rfnm_bufdesc_rx) * RFNM_ADC_BUFCNT) + SZ_64M, 
			rfnm_rx_usb_buf, sizeof(struct rfnm_rx_usb_buf) * RFNM_RX_USB_BUF_SIZE);


	//rfnm_rx_usb_buf = kzalloc((sizeof(struct rfnm_rx_usb_buf) * RFNM_RX_USB_BUF_SIZE), GFP_KERNEL);
	rfnm_m7_status = (struct rfnm_m7_status *) ioremap((0x00900000 + 0x1000), SZ_4K);



	




	spin_lock_init(&rfnm_dev->rx_usb_cb.reader_lock);
	spin_lock_init(&rfnm_dev->rx_usb_cb.writer_lock);



	

	rfnm_reset_sm();
	

	rfnm_usb_req_buffer_in = kzalloc(sizeof(struct rfnm_usb_req_buffer), GFP_KERNEL);
	if (!rfnm_usb_req_buffer_in)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&rfnm_usb_req_buffer_in->active);

	rfnm_usb_req_buffer_in_usb = kzalloc(sizeof(struct rfnm_usb_req_buffer), GFP_KERNEL);
	if (!rfnm_usb_req_buffer_in_usb)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&rfnm_usb_req_buffer_in_usb->active);

	rfnm_usb_req_buffer_out_usb = kzalloc(sizeof(struct rfnm_usb_req_buffer), GFP_KERNEL);
	if (!rfnm_usb_req_buffer_out_usb)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&rfnm_usb_req_buffer_out_usb->active);

	rfnm_usb_req_buffer_out = kzalloc(sizeof(struct rfnm_usb_req_buffer), GFP_KERNEL);
	if (!rfnm_usb_req_buffer_out)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&rfnm_usb_req_buffer_out->active);

	
	spin_lock_init(&rfnm_usb_req_buffer_in->list_lock);
	spin_lock_init(&rfnm_usb_req_buffer_out->list_lock);

	
	/*err = usb_gadget_probe_driver(&rfnm_usb_driver);
	if (err < 0)
		dev_err(la9310_dev->dev, "Failed to register USB driver\n");*/


	// callback should be called when certain everything is inited
	err = rfnm_callback_init(la9310_dev);
	if (err < 0)
		dev_err(la9310_dev->dev, "Failed to register RFNM Callback\n");


	//schedule_work(&rfnm_tasklet_in);

	//tasklet_schedule(&rfnm_tasklet_in);

#if 0
	kthread_run(rfnm_handler_in, NULL, "RX");
	kthread_run(rfnm_handler_out, NULL, "TX");
	kthread_run(rfnm_handler_usb, NULL, "USB");
#else
	start_sm();
#endif

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

	

	stop_sm();

	kfree(rfnm_dev);
	kfree(tmp_usb_buffer_copy_to_be_deprecated);
	//kfree(rfnm_rx_usb_buf);
	

	//tasklet_kill(&rfnm_tasklet_in);

	debugfs_remove_recursive(dfs_rfnm_dir);

	/*usb_gadget_unregister_driver(&rfnm_usb_driver);*/


	//hrtimer_cancel(&test_hrtimer);


}

MODULE_PARM_DESC(device, "LA9310 Device name(wlan_monX)");
module_init(la9310_rfnm_init);
module_exit(la9310_rfnm_exit);
MODULE_LICENSE("GPL");




