// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
/*
 * Copyright 2017, 2021-2022 NXP
 */

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

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/buffer_impl.h>
#include <linux/iio/buffer-dma.h>
#include <linux/iio/buffer-dmaengine.h>
#include <linux/list.h>

struct list_head iio_rfnm_submitted_buffer;
/*
struct iio_rfnm_submitted_buffer {
	struct list_head list;
	struct iio_rfnm_buffer *buffer;
}
*/
static int iio_rfnm_read_raw(struct iio_dev *indio_dev,
			   const struct iio_chan_spec *chan,
			   int *val, int *val2, long info)
{
	switch (info) {
	case IIO_CHAN_INFO_SAMP_FREQ:

		*val = 122880000;

		return IIO_VAL_INT;
	}
	return -EINVAL;
}

static int iio_rfnm_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long info)
{
	return -EINVAL;
}

static const struct iio_info iio_rfnm_chinfo = {
	.read_raw = iio_rfnm_read_raw,
	.write_raw = iio_rfnm_write_raw,
};

struct iio_dev *indio_dev;





struct rfnm_iio {
	int hello;
};

struct rfnm_iio *rfnm_iio;


struct iio_rfnm_buffer {
	struct iio_dma_buffer_queue queue;
	struct list_head active;
	size_t align;
	size_t max_size;
};

struct list_head * iio_rfnm_active_list_workaround_pointer = 0;



int packet_dump = 1;			/* Default: Dump the packet */




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

#define RFNM_IQFLOOD_BUFCNT 16
#define RFNM_IQFLOOD_BUFSIZE (1024*1024*2)

#define RFNM_IQFLOOD_CBSIZE (RFNM_IQFLOOD_BUFSIZE * 8)

dma_addr_t rfnm_iqflood_dma_addr;

// from external define files

//#define IQ_DATA_OFFSET ( 0x219100 ) 
//#define SCRATCH_BUF_ADDR 0x92400000

void __iomem *gpio4_iomem;
volatile unsigned int *gpio4;
int gpio4_initial;

int enable_callback = 0;


volatile int next_iqflood_write_buf = 0;

uint8_t *rfnm_iqflood_buf[RFNM_IQFLOOD_BUFCNT];

uint8_t * rfnm_iqflood_vmem;
uint8_t * rfnm_iqflood_vmem_nocache;
uint32_t * tmp32a;
uint32_t * tmp32b;
uint32_t * tmp32c;

// 0x1F510000??

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

static void iio_rfnm_buffer_block_done(void *data)
{
	struct iio_dma_buffer_block *block = data;
	unsigned long flags;

	spin_lock_irqsave(&block->queue->list_lock, flags);
	list_del(&block->head);
	spin_unlock_irqrestore(&block->queue->list_lock, flags);
	//block->bytes_used -= result->residue;
	//block->block.bytes_used = RFNM_IQFLOOD_BUFSIZE ; // (12/16)
	iio_dma_buffer_block_done(block);
}

struct rfnm_cb {
	int head;
	int tail;
	int reader_too_slow;
	int writer_too_slow;
	spinlock_t writer_lock;
	spinlock_t reader_lock;
};

struct rfnm_cb *rfnm_cb;



static void rfnm_tasklet_handler(unsigned long tasklet_data) {
	struct iio_dma_buffer_block * block_writing_to;

	*gpio4 = *gpio4 | (0x1 << 7);
tasklet_again:

	block_writing_to = list_first_entry_or_null(iio_rfnm_active_list_workaround_pointer, struct iio_dma_buffer_block, head);

	if(block_writing_to == NULL) {
		*gpio4 = *gpio4 | (0x1 << 8); *gpio4 = *gpio4 & ~(0x1 << 8);
		goto exit_tasklet_no_unlock;
		return;
	}

	spin_lock(&rfnm_cb->reader_lock);

	int head = smp_load_acquire(&rfnm_cb->head);
	int tail = rfnm_cb->tail;
	int readable;
	uint8_t * block_writing_to_addr = block_writing_to->vaddr;//phys_to_virt(block_writing_to->phys_addr);
	
	readable = head - tail;

	if(head < tail) {
		readable += RFNM_IQFLOOD_CBSIZE; 
	}

	readable = (readable * 12) / 16;
	
	//printk("e %d %d %d", head, tail, readable);
	
	
	if(readable < block_writing_to->block.size) {
		// we only want full outgoing buffers but it's not tecnically a requirement
		*gpio4 = *gpio4 | (0x1 << 9); *gpio4 = *gpio4 & ~(0x1 << 9);
		goto exit_tasklet;
	}
	
	if(tail + block_writing_to->block.size > RFNM_IQFLOOD_CBSIZE) {	
		unsigned long first_read_size = RFNM_IQFLOOD_CBSIZE - tail;

		//pack16to12(block_writing_to_addr, (uint64_t *)(uint8_t *) rfnm_iqflood_vmem + tail, first_read_size);
		//pack16to12(block_writing_to_addr + first_write_size, (uint64_t *)(uint8_t *) rfnm_iqflood_vmem, block_writing_to->block.size - first_read_size);
		tail += ((block_writing_to->block.size * 16) / 12) - RFNM_IQFLOOD_CBSIZE;
		//printk("- %d %d %d", head, tail, readable);
	} else {
		//pack16to12(block_writing_to_addr, (uint64_t *)(uint8_t *) rfnm_iqflood_vmem + tail, block_writing_to->block.size);
		tail += (block_writing_to->block.size * 16) / 12;
		
	}
	//printk("o %d %d %d", head, tail, readable);

	block_writing_to->block.bytes_used = block_writing_to->block.size;
	
	smp_store_release(&rfnm_cb->tail, tail);

	spin_unlock(&rfnm_cb->reader_lock);

	*gpio4 = *gpio4 | (0x1 << 0); *gpio4 = *gpio4 & ~(0x1 << 0);
	iio_rfnm_buffer_block_done(block_writing_to);

	goto tasklet_again;

exit_tasklet: 
	spin_unlock(&rfnm_cb->reader_lock);
exit_tasklet_no_unlock:
	*gpio4 = *gpio4 & ~(0x1 << 7);
}

static DECLARE_TASKLET_OLD(rfnm_tasklet, &rfnm_tasklet_handler);


void callback_func(struct device *dev)
{
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

	


	
	
	/* todo: improve cache */
	//flush_cache_all();

	
	//dcache_clean_poc(start, start + RFNM_IQFLOOD_MEMSIZE);


	//for(i = 0; i < 100; i++) {
		
		//printk("asd\n");
	//}



	//arch_sync_dma_for_cpu(0x96400000lu, RFNM_IQFLOOD_MEMSIZE, 0);
	//arch_sync_dma_for_device(RFNM_IQFLOOD_MEMADDR, RFNM_IQFLOOD_MEMSIZE, 0);
	
	/*
	start = (unsigned long)phys_to_virt(RFNM_IQFLOOD_MEMADDR);
	//dcache_clean_poc(start, start + RFNM_IQFLOOD_MEMSIZE);
	dcache_inval_poc(start, start + RFNM_IQFLOOD_MEMSIZE);
	*/
	

	//arch_sync_dma_for_cpu


	

	//uint32_t *p;

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


	spin_lock(&rfnm_cb->writer_lock);

	int head = rfnm_cb->head;
	int tail = READ_ONCE(rfnm_cb->tail);

	if(head < tail) {
		head += RFNM_IQFLOOD_CBSIZE;
	}

	if(head - tail > (RFNM_IQFLOOD_BUFSIZE / 2)) {
		rfnm_cb->reader_too_slow++;
	}

	head = this_rcv_buf * RFNM_IQFLOOD_BUFSIZE;

	smp_store_release(&rfnm_cb->head, head);
	//wake_up(consumer);

	spin_unlock(&rfnm_cb->writer_lock);





	if(1 && ++countdown_to_print >= 100) {
		countdown_to_print = 0;


		tmp32b = (uint32_t *) rfnm_iqflood_buf[next_iqflood_write_buf];
		tmp32a = (uint32_t *) ((uint8_t *) rfnm_iqflood_vmem + (this_rcv_buf * RFNM_IQFLOOD_BUFSIZE));
		tmp32c = (uint32_t *)((uint8_t *) rfnm_iqflood_vmem + (1024*1024*17));

		if(0)
		printk("\n%08x %08x %08x %08x %08x %08x %08x %08x\n%08x %08x %08x %08x %08x %08x %08x %08x\n%08x",
			*(tmp32a + 0), 
			*(tmp32a + 1),
			*(tmp32a + 2),
			*(tmp32a + 3),
			*(tmp32a + 4),
			*(tmp32a + 5),
			*(tmp32a + 6),
			*(tmp32a + 7),

			*(tmp32b + 0), 
			*(tmp32b + 1),
			*(tmp32b + 2),
			*(tmp32b + 3),
			*(tmp32b + 4),
			*(tmp32b + 5),
			*(tmp32b + 6),
			*(tmp32b + 7),
			*(tmp32c)
		);

		

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
			received_data_diff / (1000*1000), data_rate, callback_cnt, rfnm_cb->reader_too_slow,
			 rfnm_cb->writer_too_slow, dropped_count, rfnm_cb->head, rfnm_cb->tail);

	}

	

	//iio_rfnm_buffer_block_done(block_writing_to);

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

	printk("rfnm_callback_init\n");

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

	for(i = 0; i < RFNM_IQFLOOD_BUFCNT; i++) {
		kfree(rfnm_iqflood_buf[i]);
	}

	enable_callback = 0;




	iio_device_unregister(indio_dev);
	iio_device_free(indio_dev);
	
	




}

/*
struct rfnm_dev {
	int hello;
};

struct rfnm_dev *rfnm_dev;
*/




//int dma_declare_coherent_memory(struct device *dev, phys_addr_t phys_addr, 				dma_addr_t device_addr, size_t size);

#define RFNM_LA9310_CHMOD(_chan, _mod, _si)			\
	{ .type = IIO_VOLTAGE,						\
	  .indexed = 1,							\
	  .modified = 1,						\
	  .channel = _chan,						\
	  .channel2 = _mod,						\
	  .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ),\
	  .scan_index = _si,						\
	  .scan_type = {						\
		.sign = 'S',						\
		.realbits = 16,					\
		.storagebits = 16,					\
		.shift = 0,						\
	  }								\
	}



static const struct iio_chan_spec iio_rfnm_channels[] = {
	RFNM_LA9310_CHMOD(0, IIO_MOD_I, 0),
	RFNM_LA9310_CHMOD(0, IIO_MOD_Q, 1)
};





static struct iio_rfnm_buffer *iio_buffer_to_rfnm_buffer(struct iio_buffer *buffer)
{
	return container_of(buffer, struct iio_rfnm_buffer, queue.buffer);
}


static ssize_t iio_rfnm_buffer_get_length_align(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct iio_buffer *buffer = to_iio_dev_attr(attr)->buffer;
	struct iio_rfnm_buffer *iio_rfnm_buffer = iio_buffer_to_rfnm_buffer(buffer);

	return sysfs_emit(buf, "%zu\n", iio_rfnm_buffer->align);
}

static IIO_DEVICE_ATTR(length_align_bytes, 0444,
		       iio_rfnm_buffer_get_length_align, NULL, 0);

static const struct attribute *iio_rfnm_buffer_attrs[] = {
	&iio_dev_attr_length_align_bytes.dev_attr.attr,
	NULL,
};













static int iio_rfnm_buffer_submit_block(struct iio_dma_buffer_queue *queue, struct iio_dma_buffer_block *block) //, int direction
{
	struct iio_rfnm_buffer *iio_rfnm_buffer = iio_buffer_to_rfnm_buffer(&queue->buffer);

	//printk("Block submitted with size %d bytes_used %d offset %d phys_addr %llx vaddr %p \n", block->block.size, block->block.bytes_used, 
	//block->block.data.offset, block->phys_addr, block->vaddr);

	/*struct iio_rfnm_submitted_buffer * new_submitted_buffer;

	new_submitted_buffer = kmalloc(sizeof(struct iio_rfnm_submitted_buffer *), GFP_KERNEL);

	new_submitted_buffer->buffer = block;

	list_add(&new_submitted_buffer->list, &iio_rfnm_submitted_buffer);
*/

	//block->block.bytes_used = RFNM_IQFLOOD_BUFSIZE;
	//block->phys_addr
	//iio_rfnm_buffer_block_done()

	*gpio4 = *gpio4 | (0x1 << 1); *gpio4 = *gpio4 & ~(0x1 << 1);

	spin_lock_irq(&iio_rfnm_buffer->queue.list_lock);
	list_add_tail(&block->head, &iio_rfnm_buffer->active);
	spin_unlock_irq(&iio_rfnm_buffer->queue.list_lock);

	tasklet_schedule(&rfnm_tasklet);

	return 0;
}

static void iio_rfnm_buffer_abort(struct iio_dma_buffer_queue *queue)
{
	struct iio_rfnm_buffer *iio_rfnm_buffer = iio_buffer_to_rfnm_buffer(&queue->buffer);

	printk("abort called\n");

	iio_dma_buffer_block_list_abort(queue, &iio_rfnm_buffer->active);
}

static void iio_rfnm_buffer_release(struct iio_buffer *buf)
{
	struct iio_rfnm_buffer *iio_rfnm_buffer = iio_buffer_to_rfnm_buffer(buf);

	iio_dma_buffer_release(&iio_rfnm_buffer->queue);
	kfree(iio_rfnm_buffer);
}


int iio_rfnm_buffer_set_length(struct iio_buffer *buffer, unsigned int length)
{

	struct la9310_dev *la9310_dev;
	la9310_dev = get_la9310_dev_byname("nlm0");


	/* Avoid an invalid state */
	if (length < 2)
		length = 2;
	buffer->length = length;
	//buffer->length = RFNM_IQFLOOD_BUFSIZE;
	//printk("length requested as %d forced to %d\n", length, RFNM_IQFLOOD_BUFSIZE);

	if(length < ((RFNM_IQFLOOD_BUFSIZE * 16) / 36)) {
		dev_err(la9310_dev->dev, "Your interface has passed in a buffer size that is too small. Buffer size is a confusing and misleading name, \
it's also know as sample count [x where buffer size in bytes = x * bytes per sample]. Increase it (%d is ideal), or you will \
keep seeing data drop.", (RFNM_IQFLOOD_BUFSIZE * 16) / 36);
	}

	if(length > ((RFNM_IQFLOOD_BUFSIZE * 16) / 36)) {
		dev_err(la9310_dev->dev, "Buffer size too big, unsupported will lead to dropped data");
	}


	

	printk("length requested as %d \n", length);
	//dump_stack();

	return 0;
}
EXPORT_SYMBOL_GPL(iio_rfnm_buffer_set_length);


static const struct iio_buffer_access_funcs iio_rfnm_buffer_ops = {
	.read = iio_dma_buffer_read,
	.write = iio_dma_buffer_write,
	.set_bytes_per_datum = iio_dma_buffer_set_bytes_per_datum,
	.set_length = iio_rfnm_buffer_set_length,
	//.request_update = iio_dma_buffer_request_update,
	.enable = iio_dma_buffer_enable,
	.disable = iio_dma_buffer_disable,
	.data_available = iio_dma_buffer_data_available,
	.space_available = iio_dma_buffer_space_available,
	.release = iio_rfnm_buffer_release,

	.alloc_blocks = iio_dma_buffer_alloc_blocks,
	.free_blocks = iio_dma_buffer_free_blocks,
	.query_block = iio_dma_buffer_query_block,
	.enqueue_block = iio_dma_buffer_enqueue_block,
	.dequeue_block = iio_dma_buffer_dequeue_block,
	.mmap = iio_dma_buffer_mmap,

	.modes = INDIO_BUFFER_HARDWARE,
	.flags = INDIO_BUFFER_FLAG_FIXED_WATERMARK,
};


static const struct iio_dma_buffer_ops iio_rfnm_default_ops = {
	.submit = iio_rfnm_buffer_submit_block,
	.abort = iio_rfnm_buffer_abort,
};




static struct iio_buffer *iio_rfnm_buffer_alloc(struct device *dev,
	//const char *channel, 
	const struct iio_dma_buffer_ops *ops,
	void *driver_data )
{
	struct iio_rfnm_buffer *iio_rfnm_buffer;
	//int ret;

	iio_rfnm_buffer = kzalloc(sizeof(*iio_rfnm_buffer), GFP_KERNEL);
	if (!iio_rfnm_buffer)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&iio_rfnm_buffer->active);

	iio_rfnm_active_list_workaround_pointer = &iio_rfnm_buffer->active;

	//iio_rfnm_buffer->chan = chan;
	iio_rfnm_buffer->align = 4096;
	//dmaengine_buffer->align = width;
	//iio_rfnm_buffer->max_size = dma_get_max_seg_size(chan->device->dev);
	iio_rfnm_buffer->max_size = RFNM_IQFLOOD_BUFSIZE;

	iio_dma_buffer_init(&iio_rfnm_buffer->queue, dev, &iio_rfnm_default_ops, driver_data); //&iio_rfnm_default_ops);

	iio_rfnm_buffer->queue.buffer.attrs = iio_rfnm_buffer_attrs;
	iio_rfnm_buffer->queue.buffer.access = &iio_rfnm_buffer_ops;

	return &iio_rfnm_buffer->queue.buffer;

//err_free:
//	kfree(iio_rfnm_buffer);
//	return ERR_PTR(ret);
}

/*
todo add to module de-init
static void iio_rfnm_buffer_free(struct iio_buffer *buffer)
{
	struct iio_rfnm_buffer *iio_rfnm_buffer = iio_buffer_to_rfnm_buffer(buffer);

	iio_dma_buffer_exit(&iio_rfnm_buffer->queue);
	iio_buffer_put(buffer);
}
*/


struct iio_rfnm_state {
	struct spi_device	*us;
	struct mutex		buf_lock;
};

static int __init la9310_rfnm_init(void)
{
	int err = 0, i;
	struct la9310_dev *la9310_dev;
	//unsigned long attr;
    //unsigned long start;

	struct iio_buffer *buffer;

	struct iio_rfnm_state *st;

	

	INIT_LIST_HEAD(&iio_rfnm_submitted_buffer);
	
	


	la9310_dev = get_la9310_dev_byname("nlm0");
	if (la9310_dev == NULL) {
		pr_err("No LA9310 device named nlm0\n");
		return -ENODEV;
	}

	

	for(i = 0; i < RFNM_IQFLOOD_BUFCNT; i++) {
		rfnm_iqflood_buf[i] = kmalloc(RFNM_IQFLOOD_BUFSIZE, GFP_KERNEL);
		if(!rfnm_iqflood_buf[i]) {
			dev_err(la9310_dev->dev, "Failed to allocate memory for I/Q buffer\n");
			err = ENOMEM;
		}
	}

	
	rfnm_iqflood_vmem_nocache = ioremap(RFNM_IQFLOOD_MEMADDR, RFNM_IQFLOOD_MEMSIZE * 10);
	rfnm_iqflood_vmem = memremap(RFNM_IQFLOOD_MEMADDR, RFNM_IQFLOOD_MEMSIZE, MEMREMAP_WB ); 
//	rfnm_iqflood_vmem = ioremap(RFNM_IQFLOOD_MEMADDR, RFNM_IQFLOOD_MEMSIZE); 

	

	/*err = dma_declare_coherent_memory(la9310_dev->dev, RFNM_IQFLOOD_MEMADDR, RFNM_IQFLOOD_MEMADDR, RFNM_IQFLOOD_MEMSIZE);
	if (err) {
		pr_err("Error dma_declare_coherent_memory");
		return err;
	}*/

	/*
	
	dma_declare_coherent_memory is not an exported symbol, so rely on size to hope linux 
	maps this properly from the physical addresses defined in the dts file
	
	*/
/*if (dma_set_mask_and_coherent(la9310_dev->dev, DMA_BIT_MASK(64))) {
		dev_err(la9310_dev->dev, "mydev: No suitable DMA available\n");
	}*/
	/*attr = DMA_ATTR_WEAK_ORDERING | DMA_ATTR_FORCE_CONTIGUOUS;
	rfnm_iqflood_vmem = dma_alloc_coherent(la9310_dev->dev, RFNM_IQFLOOD_MEMSIZE, &rfnm_iqflood_dma_addr, attr);

	*/


	if(!rfnm_iqflood_vmem) {
		dev_err(la9310_dev->dev, "Failed to map I/Q buffer\n");
		err = ENOMEM;
	}
	

	//rfnm_iqflood_vmem = phys_to_virt(RFNM_IQFLOOD_MEMADDR);

	dev_info(la9310_dev->dev, "Mapped IQflood from %x to %p\n", RFNM_IQFLOOD_MEMADDR, rfnm_iqflood_vmem);


	//for(i = 0; i < 1000; i++) {
	//	callback_func(i & 1);
	//}
/*
	//start = (unsigned long)phys_to_virt(RFNM_IQFLOOD_MEMADDR);
	start = (unsigned long)rfnm_iqflood_vmem;
	//dcache_clean_poc(start, start + RFNM_IQFLOOD_MEMSIZE);


	for(i = 0; i < 100; i++) {
		dcache_inval_poc(start, start + RFNM_IQFLOOD_MEMSIZE);
		//printk("asd\n");
	}
*/	
	


	


	gpio4_iomem = ioremap(0x30230000, SZ_4K);

	gpio4 = (volatile unsigned int *) gpio4_iomem;

	gpio4_initial = *gpio4;

	// 1000 = 1.06us







	//rfnm_dev = vmalloc(sizeof(struct rfnm_dev));


	
printk("asd %lu\n", sizeof(la9310_dev->dev));
	indio_dev = iio_device_alloc(la9310_dev->dev, sizeof(*st));
	if (!indio_dev) {
		dev_err(la9310_dev->dev, "failed iio_device_alloc\n");
	}

	indio_dev->dev.parent = la9310_dev->dev;
	indio_dev->name = "rfnm";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = iio_rfnm_channels;
	indio_dev->num_channels = ARRAY_SIZE(iio_rfnm_channels);
	indio_dev->info = &iio_rfnm_chinfo;

	











	

	buffer = iio_rfnm_buffer_alloc(indio_dev->dev.parent, NULL, NULL);
	if (IS_ERR(buffer)) {
		dev_err(la9310_dev->dev, "failed iio_rfnm_buffer_alloc\n");
	}

	indio_dev->modes |= INDIO_BUFFER_HARDWARE;

	err = iio_device_attach_buffer(indio_dev, buffer);
	if(err) {
		dev_err(la9310_dev->dev, "failed to iio_device_attach_buffer\n");
	}






	err = iio_device_register(indio_dev);
	if(err) {
		dev_err(la9310_dev->dev, "failed iio_device_register\n");
	}

	rfnm_cb = kzalloc(sizeof(struct rfnm_cb), GFP_KERNEL);


	spin_lock_init(&rfnm_cb->reader_lock);
	spin_lock_init(&rfnm_cb->writer_lock);

	rfnm_cb->head = 0;
	rfnm_cb->tail = 0;


	enable_callback = 1;


	// callback should be called when certain everything is inited
	err = rfnm_callback_init(la9310_dev);
	if (err < 0)
		dev_err(la9310_dev->dev, "Failed to register RFNM Callback\n");



	return err;
}

module_param(packet_dump, int, 0);
MODULE_PARM_DESC(device, "LA9310 Device name(wlan_monX)");
module_init(la9310_rfnm_init);
module_exit(la9310_rfnm_exit);
MODULE_LICENSE("GPL");




