/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2017-2018, 2021 NXP
 */

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/version.h>
#include "la9310_base.h"
#include "la9310_v2h_callback.h"
#include "la9310_v2h_if.h"

#define V2H_ALIGNMENT		4096

struct register_callback {
	v2h_callback_t callback_fptr;
	void *cookie;
};

struct la9310_v2h_pkt_stats {
	uint32_t v2h_rcvd_pkts;
	uint32_t v2h_mem_fail;
	uint32_t v2h_tasklet_reschedule_count;
	uint32_t v2h_rcvd_max_pkts_per_tasklet;
	uint32_t v2h_rcvd_pkts_per_tasklet;
};

struct la9310_v2h_dev {
	uint32_t ring_head;
	struct v2h_ring *ring_ptr;
	struct register_callback *callback_info;
	struct tasklet_struct v2h_tasklet;
	struct la9310_v2h_pkt_stats rcvd_pkt;
};

static void v2h_tasklet_handler(unsigned long data);

static ssize_t
la9310_show_v2h_pkt_stats(void *v2h_dev, char *buf,
			  struct la9310_dev *la9310_dev)
{
	int print_len;
	int i;
	struct la9310_v2h_dev *v2h_priv_dev;

	v2h_priv_dev = (struct la9310_v2h_dev *) v2h_dev;

	if ((la9310_dev->stats_control & (1 << HOST_CONTROL_V2H_STATS)) == 0)
		return 0;

	print_len = sprintf(buf, "Host V2H stats:\n");
	print_len += sprintf((buf + print_len), "v2h_rcvd_pkts: %7u\n",
			     v2h_priv_dev->rcvd_pkt.v2h_rcvd_pkts);
	print_len += sprintf((buf + print_len), "v2h_mem_fail: %7u\n",
			     v2h_priv_dev->rcvd_pkt.v2h_mem_fail);
	print_len += sprintf((buf + print_len),
			     "v2h_tasklet_reschedule_count: %7u\n",
			     v2h_priv_dev->rcvd_pkt.
			     v2h_tasklet_reschedule_count);
	print_len +=
		sprintf((buf + print_len),
			"v2h_rcvd_max_pkts_per_tasklet: %7u\n",
			v2h_priv_dev->rcvd_pkt.v2h_rcvd_max_pkts_per_tasklet);
	print_len +=
		sprintf((buf + print_len), "v2h_rcvd_pkts_per_tasklet: %7u\n",
			v2h_priv_dev->rcvd_pkt.v2h_rcvd_pkts_per_tasklet);

	for (i = 0; i < V2H_MAX_BD; i++) {
		print_len += sprintf((buf + print_len),
				     "i: %d, v2h Ring owner: %d\n",
				     i,
				     v2h_priv_dev->ring_ptr->bd_info[i].
				     owner);
	}

	return print_len;
}

static void
la9310_reset_v2h_stats(void *v2h_dev)
{
	struct la9310_v2h_dev *v2h_priv_dev;

	v2h_priv_dev = (struct la9310_v2h_dev *) v2h_dev;
	v2h_priv_dev->rcvd_pkt.v2h_rcvd_pkts = 0;
	v2h_priv_dev->rcvd_pkt.v2h_mem_fail = 0;
	v2h_priv_dev->rcvd_pkt.v2h_tasklet_reschedule_count = 0;
}



static int
la9310_init_v2h_stats(struct la9310_dev *la9310_dev)
{
	struct la9310_stats_ops stats_ops;
	struct la9310_v2h_dev *v2h_dev;

	v2h_dev = la9310_dev->v2h_priv;

	stats_ops.la9310_show_stats = la9310_show_v2h_pkt_stats;
	stats_ops.la9310_reset_stats = la9310_reset_v2h_stats;
	stats_ops.stats_args = (void *) v2h_dev;
	return la9310_host_add_stats(la9310_dev, &stats_ops);
}

/*****************************************************************************
 * @register_v2h_callback
 *
 * Receiver application uses this API to receive a callback when a
 * frame available.
 *
 * name			- [IN] device name
 *
 * callbackfunc   - [IN] pointer to callback function
 *
 * skb_ptr	  - [OUT] v2h driver copy the sk_buff pointer to this..
 *
 * Return Value -
 *	SUCCESS - 0
 *	Negative value -EINVAL
 ****************************************************************************/
int
register_v2h_callback(const char *name, v2h_callback_t callbackfunc,
		      void *cookie)
{
	struct la9310_dev *la9310_dev;
	struct la9310_v2h_dev *v2h_dev;

	printk("Inside register_v2h_callback\n");

	la9310_dev = get_la9310_dev_byname(name);

	if (NULL != la9310_dev) {
		v2h_dev = la9310_dev->v2h_priv;
		if ((NULL != v2h_dev) && (NULL != v2h_dev->callback_info)) {
			v2h_dev->callback_info->callback_fptr = callbackfunc;
			v2h_dev->callback_info->cookie = cookie;

			/* Notify LA9310 that V2H callback is registered */
			la9310_set_host_ready(la9310_dev,
					      LA9310_HIF_STATUS_V2H_READY);
			return 0;
		} else {
		printk("NULL == v2h_dev) || (NULL == v2h_dev->callback_info\n");
	}
	} else {
		printk("NULL == la9310_dev\n");
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(register_v2h_callback);
/*****************************************************************************
 * @unregister_v2h_callback
 *
 * unregister callback function , set callback func pointer to null
 *
 * name			- [IN] device name
 *
 * Return Value -
 *	SUCCESS - 0
 *	Negative value -EINVAL
 ****************************************************************************/
int
unregister_v2h_callback(const char *name)
{
	struct la9310_dev *la9310_dev;
	struct la9310_v2h_dev *v2h_dev;
	int irq_num;

	la9310_dev = get_la9310_dev_byname(name);

	if (NULL != la9310_dev) {
		irq_num = la9310_get_msi_irq(la9310_dev, MSI_IRQ_V2H);
		v2h_dev = la9310_dev->v2h_priv;
		if ((NULL != v2h_dev) && (NULL != v2h_dev->callback_info)) {
			disable_irq(irq_num);
			v2h_dev->callback_info->callback_fptr = NULL;
			dev_info(la9310_dev->dev,
				 "V2H callback unregister\n");
			enable_irq(irq_num);
			return 0;
		}
	}
	return -EINVAL;

}
EXPORT_SYMBOL_GPL(unregister_v2h_callback);
/*****************************************************************************
* @allocate_frame_buffer
*
* This function allocate sk_buff meomry in DDR and return the address of
* sk_buff
* head room pointer physical address
* la9310_devr	- pointer to struct la9310_dev
*
*
* Return Value -
*      SUCCESS - 64 bit physical address of the newly allocated buffer
*      Negative value - -ENOMEM
****************************************************************************/
int
allocate_frame_buffer(struct la9310_dev *la9310_dev, phys_addr_t *phys_addr)
{
	struct sk_buff *skb;
	struct v2h_headroom *headroom_ptr;
	struct la9310_v2h_dev *v2h_dev;
	uint32_t size, alignment_reserve;
	u64 aligned_data;

	size = V2H_MAX_SKB_BUFF_SIZE + V2H_ALIGNMENT;
	v2h_dev = (struct la9310_v2h_dev *) la9310_dev->v2h_priv;

	/* Allocate sk_buff of MAX_SKB_BUFF_SIZE (2KB) size */
	skb = alloc_skb(size, GFP_ATOMIC | GFP_DMA);
	if (NULL != skb) {
		/* Initially point to head room location */
		aligned_data = ALIGN((u64) skb->data, V2H_ALIGNMENT);
		alignment_reserve = aligned_data - (u64) skb->data;
		skb_reserve(skb, alignment_reserve);
		headroom_ptr = (struct v2h_headroom *) (skb->data);
		headroom_ptr->skb_ptr = (uint64_t) skb;
		skb_put(skb, sizeof(struct v2h_headroom));
		*phys_addr = virt_to_phys(skb->data);
#ifdef V2H_DEBUG_ENABLE
		dev_info(la9310_dev->dev,
			 "%s data  %p, head %p,  phys_addr %llx,skb = %p\n",
			 __func__, skb->data, skb->head, (u64) *phys_addr,
			 skb);
#endif
	} else {
		v2h_dev->rcvd_pkt.v2h_mem_fail++;
		dev_err(la9310_dev->dev, "%s skbuff allocation fail\n",
			__func__);
		return -ENOMEM;
	}

	return 0;
}

/*****************************************************************************
* @get_v2h_pci_addr
*
* This function return pci outboud address for la9310 to dma / copy into host
* ddr
*
* index   - [IN] pci address map to be indexed
*
*
* Return Value -
*      SUCCESS - pci istart address of the indexed memory
*      Fail - 0
 ****************************************************************************/
uint32_t
get_v2h_pci_addr(int index)
{
	if (index >= V2H_MAX_BD)
		return 0;
	else
		return (LA9310_V2H_PCI_ADDR_BASE + 4096 * index);
}

/*****************************************************************************
 * @init_rxpkt_ring_buff
 *
 * This function initially allocates sk_buff memory in DDR for all the BDs
 * set the owner ship of the return buffer to VSPA
 *
 * ring_ptr   - [IN]  BD ring pointer in tcm memory
 *
 * Return Value -
 *	SUCCESS - 0
 *	Negative value - -ENOMEM / -EINVAL
 ****************************************************************************/
int
init_rxpkt_ring_buff(struct la9310_dev *la9310_dev, struct v2h_ring *ring_ptr)
{
	uint32_t cnt, ctr;
	uint64_t phys_addr;
	void *virt_addr;
	struct v2h_headroom *headroom_ptr;
	int ret;
	/*
	 * Allocate memory for ring buffer descriptor.
	 * This ring buffer descriptor would be used between VSPA and host.
	 * Host Linux would populate the ring buffer entries and set ownership
	 * to VSPA.VSPA would fill the buffer with frame and control information
	 * and set the owner ship to Host Linux
	 */

	if (NULL != ring_ptr) {
		/* Reserve headroom of strcut v2h_headroom size */
		for (cnt = 0; cnt < V2H_MAX_BD; cnt++) {
			ret = allocate_frame_buffer(la9310_dev, &phys_addr);
			if (ret < 0) {
				dev_err(la9310_dev->dev,
					"FAIL to alloc sk buff memory cnt %d\n",
					cnt);
				/* Rollback and free all allocated BDs and
				   return memory allocation failure */
				for (ctr = 0; ctr < cnt; ctr++) {
					virt_addr =
						phys_to_virt(ring_ptr->
							     bd_info[ctr].
							     host_phys_addr);
					headroom_ptr = (struct v2h_headroom *)
						(virt_addr);
					kfree_skb((struct sk_buff *)
						  headroom_ptr->skb_ptr);
				}
				return -ENOMEM;
			} else {
				/* frame dma able address */
				ring_ptr->bd_info[cnt].host_phys_addr
					= phys_addr;
				ring_ptr->bd_info[cnt].la9310_pci_addr
					= get_v2h_pci_addr(cnt);
				ring_ptr->bd_info[cnt].owner = OWNER_VSPA;
			}
		}
	} else {
		return -EINVAL;
	}

	return 0;
}

/* Bottom Half Function - Tasklet */
void
v2h_tasklet_handler(unsigned long data)
{
	uint32_t head;
	uint32_t process_flag = 1;
	struct v2h_ring *r_ptr;
	uint64_t phys_addr;
	uint32_t headroom_size;
	void *virt_addr;
	struct v2h_headroom *headroom_ptr;
	struct v2h_frame_ctrl *frm_ctrl;
	struct sk_buff *skb;
	struct la9310_dev *la9310_dev = (struct la9310_dev *) data;
	struct la9310_v2h_dev *v2h_dev;
	struct la9310_hif *hif;
	uint32_t num_pkt_processed = 0;
	int ret;
	int callBackSet = 1;

	v2h_dev = (struct la9310_v2h_dev *) la9310_dev->v2h_priv;
	head = v2h_dev->ring_head;
	r_ptr = v2h_dev->ring_ptr;
	headroom_size = sizeof(struct v2h_headroom);
	hif = la9310_dev->hif;

	/* Process all the frames received from VSPA */
	while (process_flag) {
		if (r_ptr->bd_info[head].owner == OWNER_HOST) {
			/* Invoke callback function with sk_buff pointer */
			if ((NULL != v2h_dev->callback_info) &&
			    (NULL != v2h_dev->callback_info->callback_fptr)) {
				virt_addr =
					phys_to_virt(r_ptr->bd_info[head].
						     host_phys_addr);
				headroom_ptr =
					(struct v2h_headroom *) (virt_addr);
				pci_map_single(la9310_dev->pdev, virt_addr,
					       V2H_MAX_SKB_BUFF_SIZE,
					       PCI_DMA_FROMDEVICE);
				dma_wmb();
				skb = (struct sk_buff *) headroom_ptr->
					skb_ptr;
				frm_ctrl = &headroom_ptr->frm_ctrl;
				skb_put(skb, frm_ctrl->len);
				/*Pull the headroom before giving to callback */
				skb_pull(skb, sizeof(struct v2h_headroom));
				v2h_dev->callback_info->callback_fptr(skb,
								      v2h_dev->
								callback_info->
								      cookie);
				/* Allocate new sk_buff and assigh pointer to
				   sk_buff->data, set ownership again to VSPA */
				ret = allocate_frame_buffer(la9310_dev,
							    &phys_addr);
				if (ret == 0) {
					r_ptr->bd_info[head].host_phys_addr =
						phys_addr;
					dma_wmb();
					r_ptr->bd_info[head].owner =
						OWNER_VSPA;
					dma_wmb();
					/* Increment the head pointer */
					head = (head + 1) % V2H_MAX_BD;
				} else {
					dev_info(la9310_dev->dev,
						 "V2H mem fail\n");
				}
				dma_wmb();
				v2h_dev->rcvd_pkt.v2h_rcvd_pkts++;
				num_pkt_processed++;
				dma_wmb();
			} else {
				callBackSet = 0;
				dev_err(la9310_dev->dev,
					"V2H callback not registered\n");
				process_flag = 0;
			}
		} else {
			process_flag = 0;
		}
	}
	/* Update the head pointer */
	v2h_dev->ring_head = head;
	la9310_dev->v2h_priv = v2h_dev;
	if (callBackSet) {
		if (r_ptr->bd_info[head].owner == OWNER_HOST) {
			v2h_dev->rcvd_pkt.v2h_tasklet_reschedule_count++;
			la9310_dev->v2h_priv = v2h_dev;
			tasklet_hi_schedule(&v2h_dev->v2h_tasklet);
		} else {
			if (num_pkt_processed >
			    v2h_dev->rcvd_pkt.v2h_rcvd_max_pkts_per_tasklet)
				v2h_dev->rcvd_pkt.
					v2h_rcvd_max_pkts_per_tasklet =
					num_pkt_processed;
			if (num_pkt_processed > 1)
				v2h_dev->rcvd_pkt.v2h_rcvd_pkts_per_tasklet++;
			hif->stats.v2h_intr_enabled = 1;
		}
	}
}

/*****************************************************************************
 * @intr_v2h_handler
 *
 * This is interrupt service routine.
 * Invoked when MSI interrupt is raised
 *
 ****************************************************************************/
static irqreturn_t
intr_v2h_handler(int irq, void *dev)
{
	struct la9310_dev *la9310_dev = (struct la9310_dev *) dev;
	struct la9310_v2h_dev *v2h_dev;
	struct la9310_hif *hif = la9310_dev->hif;

	hif->stats.v2h_intr_enabled = 0;
	v2h_dev = (struct la9310_v2h_dev *) la9310_dev->v2h_priv;
	/* BH scheduled */
	tasklet_hi_schedule(&v2h_dev->v2h_tasklet);
	return IRQ_HANDLED;
}

/*****************************************************************************
 * @la9310_v2h_remove
 *
 * This function should be called when unloading the V2H driver.
 ****************************************************************************/
int
la9310_v2h_remove(struct la9310_dev *la9310_dev)
{
	int rc = 0;
	struct la9310_v2h_dev *v2h_dev;
	int irq_num;
	void *virt_addr;
	struct v2h_headroom *headroom_ptr;
	int ctr;
	struct la9310_hif *hif;

	dev_dbg(la9310_dev->dev, "V2H DRIVER removed\n");
	/*Disable interrupt invocation from CM4/VSPA */
	hif = la9310_dev->hif;
	hif->stats.v2h_intr_enabled = 0;
	v2h_dev = (struct la9310_v2h_dev *) la9310_dev->v2h_priv;
	if (NULL != v2h_dev) {
		tasklet_kill(&v2h_dev->v2h_tasklet);
		for (ctr = 0; ctr < V2H_MAX_BD; ctr++) {
			virt_addr =
				phys_to_virt(v2h_dev->ring_ptr->bd_info[ctr].
					     host_phys_addr);
			headroom_ptr = (struct v2h_headroom *) (virt_addr);
			kfree_skb((struct sk_buff *) headroom_ptr->skb_ptr);
		}
		irq_num = la9310_get_msi_irq(la9310_dev, MSI_IRQ_V2H);
		if (NULL != v2h_dev->callback_info) {
			disable_irq(irq_num);
			v2h_dev->callback_info->callback_fptr = NULL;
			kfree(v2h_dev->callback_info);
			enable_irq(irq_num);
		}
		free_irq(la9310_get_msi_irq(la9310_dev, MSI_IRQ_V2H),
			 la9310_dev);
		kfree(v2h_dev);
	}
	return rc;
}

/*****************************************************************************
 * @la9310_v2h_probe
 *
 * This function would be called by la9310 driver when loading the V2H driver.
 ****************************************************************************/
int
la9310_v2h_probe(struct la9310_dev *la9310_dev, int virq_count,
		 struct virq_evt_map *virq_map)
{
	int err = 0, ret = 0;
	struct la9310_mem_region_info *tcm_region;
	struct v2h_ring *ring_ptr;
	u32 len;

	struct la9310_v2h_dev *v2h_dev = NULL;

	v2h_dev = kmalloc(sizeof(struct la9310_v2h_dev), GFP_KERNEL);
	if (!v2h_dev) {
		ret = -ENOMEM;
		goto fail;
	}

	memset(v2h_dev, 0x00, sizeof(struct la9310_v2h_dev));
	dev_dbg(la9310_dev->dev, "V2H DRIVER Probed\n");
	tcm_region = &la9310_dev->mem_regions[LA9310_MEM_REGION_TCMU];
	len = sizeof(struct v2h_ring);

	/* Ring pointer in tcm memory */
	ring_ptr = (struct v2h_ring *) ((u8 *) tcm_region->vaddr +
					LA9310_V2H_RING_OFFSET);
	dev_dbg(la9310_dev->dev, "V2H ring ptr addr %p\n", ring_ptr);
	/* Initialize all the BD's with pre allocated sk_buff->data
	   and set the owner ship to VSPA */
	ret = init_rxpkt_ring_buff(la9310_dev, ring_ptr);
	if (ret < 0) {
		dev_err(la9310_dev->dev,
			"SKB Memory allocation failure for v2h_dev\n");
		ret = -ENOMEM;
		goto fail;
	}

	/* Set the priv data structure with ring pointer , head etc */
	v2h_dev->ring_ptr = ring_ptr;
	v2h_dev->ring_head = 0;

	v2h_dev->callback_info = kmalloc(sizeof(struct register_callback),
					 GFP_KERNEL);
	if (!v2h_dev->callback_info) {
		ret = -ENOMEM;
		goto fail;
	} else {
		v2h_dev->callback_info->callback_fptr = NULL;
	}
	la9310_dev->v2h_priv = v2h_dev;
	dev_err(la9310_dev->dev, "virq_map->virq: %d\n", virq_map->virq);
	dev_err(la9310_dev->dev, "intr_v2h_handler: %p\n", intr_v2h_handler);
	dev_err(la9310_dev->dev, "la9310_dev: %p\n", la9310_dev);

	err = request_irq(virq_map->virq,
			  intr_v2h_handler, IRQF_NO_THREAD, "V2H interrupt",
			  la9310_dev);
	if (err < 0) {
		dev_err(la9310_dev->dev, "request_irq failed\n");
		ret = err;
		goto fail;
	}
	err = la9310_init_v2h_stats(la9310_dev);
	if (err < 0) {
		ret = err;
		goto fail;
	}
	tasklet_init(&v2h_dev->v2h_tasklet, v2h_tasklet_handler,
		     (unsigned long int) la9310_dev);
	return ret;
fail:
	/* if failed , for any reason  */
	if ((NULL != v2h_dev) && (NULL != v2h_dev->callback_info))
		kfree(v2h_dev->callback_info);

	if (NULL != v2h_dev)
		kfree(v2h_dev);

	la9310_v2h_remove(la9310_dev);

	return ret;
}

