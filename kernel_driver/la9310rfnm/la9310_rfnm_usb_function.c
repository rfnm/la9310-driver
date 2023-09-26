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

#include <linux/usb/composite.h>
#include "/home/davide/imx-rfnm-bsp/build/tmp/work-shared/imx8mp-rfnm/kernel-source/drivers/usb/gadget/function/g_zero.h"
#include "/home/davide/imx-rfnm-bsp/build/tmp/work-shared/imx8mp-rfnm/kernel-source/drivers/usb/gadget/u_f.h"


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

#define RFNM_IQFLOOD_BUFSIZE (1024*1024*2)
#define RFNM_IQFLOOD_CBSIZE (RFNM_IQFLOOD_BUFSIZE * 8)

void __iomem *gpio4_iomem;
volatile unsigned int *gpio4;
int gpio4_initial;

uint8_t * rfnm_iqflood_vmem;
uint8_t * rfnm_iqflood_vmem_nocache;

struct rfnm_cb {
	int head;
	int tail;
	int reader_too_slow;
	int writer_too_slow;
	spinlock_t writer_lock;
	spinlock_t reader_lock;
};


struct rfnm_dev {
	struct rfnm_cb usb_cb;
	uint8_t * usb_config_buffer;
};

#define CONFIG_DESCRIPTOR_MAX_SIZE 1000

struct rfnm_dev *rfnm_dev;


#define DRIVER_VENDOR_ID	0x0525 /* NetChip */
#define DRIVER_PRODUCT_ID	0xc0de /* undefined */

#define USB_DEBUG_MAX_PACKET_SIZE     8
#define DBGP_REQ_EP0_LEN              128
#define DBGP_REQ_LEN                  512


void pack16to12(uint8_t * dest, uint64_t * src, int cnt) {
	uint64_t buf;
	uint64_t r0;
	int32_t c;
	uint64_t *dest_64;

	cnt = cnt / 8;

	//printk("%p %p\n", dest, src);


	for(c = 0; c < cnt; c++) {
		buf = *(src + c);
		r0 = 0;
		r0 |= (buf & (0xfffl << 0)) >> 0;
		r0 |= (buf & (0xfffl << 16)) >> 4;
		r0 |= (buf & (0xfffll << 32)) >> 8;
		r0 |= (buf & (0xfffll << 48)) >> 12;

		//printk("set %llux to %p (c=%d)\n", r0,  ( void * ) (dest + (c * 3)), c);
		dest_64 = (uint64_t *) (dest + (c * 6));
		*dest_64 = r0;

	//if(c > 10)
	//	return;
	}

}

static inline size_t list_count_nodes(struct list_head *head)
{
	struct list_head *pos;
	size_t count = 0;

	list_for_each(pos, head)
		count++;

	return count;
}



static void rfnm_tasklet_handler(unsigned long tasklet_data) {
}

static DECLARE_TASKLET_OLD(rfnm_tasklet, &rfnm_tasklet_handler);


void callback_func(struct device *dev)
{
	return;

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


	spin_lock(&rfnm_dev->usb_cb.writer_lock);

	int head = rfnm_dev->usb_cb.head;
	int tail = READ_ONCE(rfnm_dev->usb_cb.tail);

	if(head < tail) {
		head += RFNM_IQFLOOD_CBSIZE;
	}

	if(head - tail > (RFNM_IQFLOOD_BUFSIZE / 2)) {
		rfnm_dev->usb_cb.reader_too_slow++;
	}

	head = this_rcv_buf * RFNM_IQFLOOD_BUFSIZE;

	smp_store_release(&rfnm_dev->usb_cb.head, head);
	spin_unlock(&rfnm_dev->usb_cb.writer_lock);


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
			received_data_diff / (1000*1000), data_rate, callback_cnt, rfnm_dev->usb_cb.reader_too_slow,
			 rfnm_dev->usb_cb.writer_too_slow, dropped_count, rfnm_dev->usb_cb.head, rfnm_dev->usb_cb.tail);

	}

	total_processing_time += ktime_get() - time_processing_start;
	*gpio4 = *gpio4 & ~(0x1 << 6);
	tasklet_schedule(&rfnm_tasklet);
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

















struct f_loopback {
	struct usb_function	function;

	struct usb_ep		*in_ep;
	struct usb_ep		*out_ep;

	unsigned                qlen;
	unsigned                buflen;
};

static inline struct f_loopback *func_to_loop(struct usb_function *f)
{
	return container_of(f, struct f_loopback, function);
}

/*-------------------------------------------------------------------------*/

static struct usb_interface_descriptor loopback_intf = {
	.bLength =		sizeof(loopback_intf),
	.bDescriptorType =	USB_DT_INTERFACE,

	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	/* .iInterface = DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor fs_loop_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_loop_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *fs_loopback_descs[] = {
	(struct usb_descriptor_header *) &loopback_intf,
	(struct usb_descriptor_header *) &fs_loop_sink_desc,
	(struct usb_descriptor_header *) &fs_loop_source_desc,
	NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor hs_loop_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor hs_loop_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_descriptor_header *hs_loopback_descs[] = {
	(struct usb_descriptor_header *) &loopback_intf,
	(struct usb_descriptor_header *) &hs_loop_source_desc,
	(struct usb_descriptor_header *) &hs_loop_sink_desc,
	NULL,
};

/* super speed support: */

static struct usb_endpoint_descriptor ss_loop_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_loop_source_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,
	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	0,
};

static struct usb_endpoint_descriptor ss_loop_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_loop_sink_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,
	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	0,
};

static struct usb_descriptor_header *ss_loopback_descs[] = {
	(struct usb_descriptor_header *) &loopback_intf,
	(struct usb_descriptor_header *) &ss_loop_source_desc,
	(struct usb_descriptor_header *) &ss_loop_source_comp_desc,
	(struct usb_descriptor_header *) &ss_loop_sink_desc,
	(struct usb_descriptor_header *) &ss_loop_sink_comp_desc,
	NULL,
};

/* function-specific strings: */

static struct usb_string strings_loopback[] = {
	[0].s = "loop input to output",
	{  }			/* end of list */
};

static struct usb_gadget_strings stringtab_loop = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_loopback,
};

static struct usb_gadget_strings *loopback_strings[] = {
	&stringtab_loop,
	NULL,
};

/*-------------------------------------------------------------------------*/

static int loopback_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_loopback	*loop = func_to_loop(f);
	int			id;
	int ret;

	/* allocate interface ID(s) */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	loopback_intf.bInterfaceNumber = id;

	id = usb_string_id(cdev);
	if (id < 0)
		return id;
	strings_loopback[0].id = id;
	loopback_intf.iInterface = id;

	/* allocate endpoints */

	loop->in_ep = usb_ep_autoconfig(cdev->gadget, &fs_loop_source_desc);
	if (!loop->in_ep) {
autoconf_fail:
		ERROR(cdev, "%s: can't autoconfigure on %s\n",
			f->name, cdev->gadget->name);
		return -ENODEV;
	}

	loop->out_ep = usb_ep_autoconfig(cdev->gadget, &fs_loop_sink_desc);
	if (!loop->out_ep)
		goto autoconf_fail;

	/* support high speed hardware */
	hs_loop_source_desc.bEndpointAddress =
		fs_loop_source_desc.bEndpointAddress;
	hs_loop_sink_desc.bEndpointAddress = fs_loop_sink_desc.bEndpointAddress;

	/* support super speed hardware */
	ss_loop_source_desc.bEndpointAddress =
		fs_loop_source_desc.bEndpointAddress;
	ss_loop_sink_desc.bEndpointAddress = fs_loop_sink_desc.bEndpointAddress;

	ret = usb_assign_descriptors(f, fs_loopback_descs, hs_loopback_descs,
			ss_loopback_descs, ss_loopback_descs);
	if (ret)
		return ret;

	DBG(cdev, "%s speed %s: IN/%s, OUT/%s\n",
	    (gadget_is_superspeed(c->cdev->gadget) ? "super" :
	     (gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full")),
			f->name, loop->in_ep->name, loop->out_ep->name);
	return 0;
}

static void lb_free_func(struct usb_function *f)
{
	struct f_lb_opts *opts;

	opts = container_of(f->fi, struct f_lb_opts, func_inst);

	mutex_lock(&opts->lock);
	opts->refcnt--;
	mutex_unlock(&opts->lock);

	usb_free_all_descriptors(f);
	kfree(func_to_loop(f));
}

static void loopback_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_loopback	*loop = ep->driver_data;
	struct usb_composite_dev *cdev = loop->function.config->cdev;
	int			status = req->status;

	switch (status) {
	case 0:				/* normal completion? */
		if (ep == loop->out_ep) {
			/*
			 * We received some data from the host so let's
			 * queue it so host can read the from our in ep
			 */
			struct usb_request *in_req = req->context;

			in_req->zero = (req->actual < req->length);
			in_req->length = req->actual;
			ep = loop->in_ep;
			req = in_req;
		} else {
			/*
			 * We have just looped back a bunch of data
			 * to host. Now let's wait for some more data.
			 */
			req = req->context;
			ep = loop->out_ep;
		}

		/* queue the buffer back to host or for next bunch of data */
		status = usb_ep_queue(ep, req, GFP_ATOMIC);
		if (status == 0) {
			return;
		} else {
			ERROR(cdev, "Unable to loop back buffer to %s: %d\n",
			      ep->name, status);
			goto free_req;
		}

		/* "should never get here" */
	default:
		ERROR(cdev, "%s loop complete --> %d, %d/%d\n", ep->name,
				status, req->actual, req->length);
		fallthrough;

	/* NOTE:  since this driver doesn't maintain an explicit record
	 * of requests it submitted (just maintains qlen count), we
	 * rely on the hardware driver to clean up on disconnect or
	 * endpoint disable.
	 */
	case -ECONNABORTED:		/* hardware forced ep reset */
	case -ECONNRESET:		/* request dequeued */
	case -ESHUTDOWN:		/* disconnect from host */
free_req:
		usb_ep_free_request(ep == loop->in_ep ?
				    loop->out_ep : loop->in_ep,
				    req->context);
		free_ep_req(ep, req);
		return;
	}
}

static void disable_loopback(struct f_loopback *loop)
{
	struct usb_composite_dev	*cdev;

	cdev = loop->function.config->cdev;
	//disable_endpoints(cdev, loop->in_ep, loop->out_ep, NULL, NULL);
	VDBG(cdev, "%s disabled\n", loop->function.name);
}

static inline struct usb_request *lb_alloc_ep_req(struct usb_ep *ep, int len)
{
	return alloc_ep_req(ep, len);
}

static int alloc_requests(struct usb_composite_dev *cdev,
			  struct f_loopback *loop)
{
	struct usb_request *in_req, *out_req;
	int i;
	int result = 0;

	/*
	 * allocate a bunch of read buffers and queue them all at once.
	 * we buffer at most 'qlen' transfers; We allocate buffers only
	 * for out transfer and reuse them in IN transfers to implement
	 * our loopback functionality
	 */
	for (i = 0; i < loop->qlen && result == 0; i++) {
		result = -ENOMEM;

		in_req = usb_ep_alloc_request(loop->in_ep, GFP_ATOMIC);
		if (!in_req)
			goto fail;

		out_req = lb_alloc_ep_req(loop->out_ep, loop->buflen);
		if (!out_req)
			goto fail_in;

		in_req->complete = loopback_complete;
		out_req->complete = loopback_complete;

		in_req->buf = out_req->buf;
		/* length will be set in complete routine */
		in_req->context = out_req;
		out_req->context = in_req;

		result = usb_ep_queue(loop->out_ep, out_req, GFP_ATOMIC);
		if (result) {
			ERROR(cdev, "%s queue req --> %d\n",
					loop->out_ep->name, result);
			goto fail_out;
		}
	}

	return 0;

fail_out:
	free_ep_req(loop->out_ep, out_req);
fail_in:
	usb_ep_free_request(loop->in_ep, in_req);
fail:
	return result;
}

static int enable_endpoint(struct usb_composite_dev *cdev,
			   struct f_loopback *loop, struct usb_ep *ep)
{
	int					result;

	result = config_ep_by_speed(cdev->gadget, &(loop->function), ep);
	if (result)
		goto out;

	result = usb_ep_enable(ep);
	if (result < 0)
		goto out;
	ep->driver_data = loop;
	result = 0;

out:
	return result;
}

static int
enable_loopback(struct usb_composite_dev *cdev, struct f_loopback *loop)
{
	int					result = 0;

	result = enable_endpoint(cdev, loop, loop->in_ep);
	if (result)
		goto out;

	result = enable_endpoint(cdev, loop, loop->out_ep);
	if (result)
		goto disable_in;

	result = alloc_requests(cdev, loop);
	if (result)
		goto disable_out;

	DBG(cdev, "%s enabled\n", loop->function.name);
	return 0;

disable_out:
	usb_ep_disable(loop->out_ep);
disable_in:
	usb_ep_disable(loop->in_ep);
out:
	return result;
}

static int loopback_set_alt(struct usb_function *f,
		unsigned intf, unsigned alt)
{
	struct f_loopback	*loop = func_to_loop(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	/* we know alt is zero */
	disable_loopback(loop);
	return enable_loopback(cdev, loop);
}

static void loopback_disable(struct usb_function *f)
{
	struct f_loopback	*loop = func_to_loop(f);

	disable_loopback(loop);
}

static struct usb_function *loopback_alloc(struct usb_function_instance *fi)
{
	struct f_loopback	*loop;
	struct f_lb_opts	*lb_opts;

	loop = kzalloc(sizeof *loop, GFP_KERNEL);
	if (!loop)
		return ERR_PTR(-ENOMEM);

	lb_opts = container_of(fi, struct f_lb_opts, func_inst);

	mutex_lock(&lb_opts->lock);
	lb_opts->refcnt++;
	mutex_unlock(&lb_opts->lock);

	loop->buflen = lb_opts->bulk_buflen;
	loop->qlen = lb_opts->qlen;
	if (!loop->qlen)
		loop->qlen = 32;

	loop->function.name = "loopback";
	loop->function.bind = loopback_bind;
	loop->function.set_alt = loopback_set_alt;
	loop->function.disable = loopback_disable;
	loop->function.strings = loopback_strings;

	loop->function.free_func = lb_free_func;

	return &loop->function;
}

static inline struct f_lb_opts *to_f_lb_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_lb_opts,
			    func_inst.group);
}

static void lb_attr_release(struct config_item *item)
{
	struct f_lb_opts *lb_opts = to_f_lb_opts(item);

	usb_put_function_instance(&lb_opts->func_inst);
}

static struct configfs_item_operations lb_item_ops = {
	.release		= lb_attr_release,
};

static ssize_t f_lb_opts_qlen_show(struct config_item *item, char *page)
{
	struct f_lb_opts *opts = to_f_lb_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%d\n", opts->qlen);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_lb_opts_qlen_store(struct config_item *item,
				    const char *page, size_t len)
{
	struct f_lb_opts *opts = to_f_lb_opts(item);
	int ret;
	u32 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou32(page, 0, &num);
	if (ret)
		goto end;

	opts->qlen = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_lb_opts_, qlen);

static ssize_t f_lb_opts_bulk_buflen_show(struct config_item *item, char *page)
{
	struct f_lb_opts *opts = to_f_lb_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%d\n", opts->bulk_buflen);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_lb_opts_bulk_buflen_store(struct config_item *item,
				    const char *page, size_t len)
{
	struct f_lb_opts *opts = to_f_lb_opts(item);
	int ret;
	u32 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou32(page, 0, &num);
	if (ret)
		goto end;

	opts->bulk_buflen = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_lb_opts_, bulk_buflen);

static struct configfs_attribute *lb_attrs[] = {
	&f_lb_opts_attr_qlen,
	&f_lb_opts_attr_bulk_buflen,
	NULL,
};

static const struct config_item_type lb_func_type = {
	.ct_item_ops    = &lb_item_ops,
	.ct_attrs	= lb_attrs,
	.ct_owner       = THIS_MODULE,
};

static void lb_free_instance(struct usb_function_instance *fi)
{
	struct f_lb_opts *lb_opts;

	lb_opts = container_of(fi, struct f_lb_opts, func_inst);
	kfree(lb_opts);
}

static struct usb_function_instance *loopback_alloc_instance(void)
{
	struct f_lb_opts *lb_opts;

	lb_opts = kzalloc(sizeof(*lb_opts), GFP_KERNEL);
	if (!lb_opts)
		return ERR_PTR(-ENOMEM);
	mutex_init(&lb_opts->lock);
	lb_opts->func_inst.free_func_inst = lb_free_instance;
	lb_opts->bulk_buflen = GZERO_BULK_BUFLEN;
	lb_opts->qlen = GZERO_QLEN;

	config_group_init_type_name(&lb_opts->func_inst.group, "",
				    &lb_func_type);

	return  &lb_opts->func_inst;
}
DECLARE_USB_FUNCTION(Loopback, loopback_alloc_instance, loopback_alloc);
















void rfnm_usb_gadget_init(struct la9310_dev *la9310_dev) {
	usb_function_register(&Loopbackusb_func);
}






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

/*
	for(i = 0; i < RFNM_IQFLOOD_BUFCNT; i++) {
		rfnm_iqflood_buf[i] = kmalloc(RFNM_IQFLOOD_BUFSIZE, GFP_KERNEL);
		if(!rfnm_iqflood_buf[i]) {
			dev_err(la9310_dev->dev, "Failed to allocate memory for I/Q buffer\n");
			err = ENOMEM;
		}
	}
*/	
	rfnm_iqflood_vmem_nocache = ioremap(RFNM_IQFLOOD_MEMADDR, RFNM_IQFLOOD_MEMSIZE * 10);
	rfnm_iqflood_vmem = memremap(RFNM_IQFLOOD_MEMADDR, RFNM_IQFLOOD_MEMSIZE, MEMREMAP_WB ); 

	if(!rfnm_iqflood_vmem) {
		dev_err(la9310_dev->dev, "Failed to map I/Q buffer\n");
		err = ENOMEM;
	}

	dev_info(la9310_dev->dev, "Mapped IQflood from %x to %p\n", RFNM_IQFLOOD_MEMADDR, rfnm_iqflood_vmem);

	gpio4_iomem = ioremap(0x30230000, SZ_4K);
	gpio4 = (volatile unsigned int *) gpio4_iomem;
	gpio4_initial = *gpio4;

	spin_lock_init(&rfnm_dev->usb_cb.reader_lock);
	spin_lock_init(&rfnm_dev->usb_cb.writer_lock);

	rfnm_dev->usb_cb.head = 0;
	rfnm_dev->usb_cb.tail = 0;


	rfnm_usb_gadget_init(la9310_dev);


	// callback should be called when certain everything is inited
	err = rfnm_callback_init(la9310_dev);
	if (err < 0)
		dev_err(la9310_dev->dev, "Failed to register RFNM Callback\n");

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

	usb_function_unregister(&Loopbackusb_func);
}

MODULE_PARM_DESC(device, "LA9310 Device name(wlan_monX)");
module_init(la9310_rfnm_init);
module_exit(la9310_rfnm_exit);
MODULE_LICENSE("GPL");




