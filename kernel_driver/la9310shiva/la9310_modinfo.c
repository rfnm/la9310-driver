/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2024 NXP
 *
 * Port of the NXP la93xx_host_sw la9310_modinfo.c onto the RFNM
 * la9310-driver fork (2026-08 stack drop). Provides the modinfo misc
 * device + IOCTL_LA93XX_MODINFO_GET so the NXP iqplayer host-utils
 * (iq_app, iq_mon, iq_trace, la9310_modem_info) work on the RFNM stack.
 *
 * Differences vs. NXP mainline, forced by the fork:
 *  - misc device is registered as "shiva%d" (what the NXP tools open),
 *    while la9310_dev->name on RFNM is "nlm%d"; open() maps the name.
 *  - scratch_buf_host_phys_addr does not exist on the fork; the scratch
 *    buffer is a fixed carveout passed via the scratch_buf_phys_addr
 *    module parameter (host phys == PCIe/ATU source on i.MX8MP 1:1 map).
 *  - iqflood is the fixed RFNM carveout from <linux/rfnm-vspa.h>
 *    (EP view LA9310_IQFLOOD_PHYS_ADDR = 0xC0000000, host
 *    RFNM_IQFLOOD_MEMADDR = 0x96400000, size RFNM_IQFLOOD_MEMSIZE).
 *    NOTE: the stock NXP iqplayer .eld addresses DDR at 0xB0001000 -
 *    see docs/IQPLAYER_ON_RFNM.md for the ATU-window implications.
 *  - rfcal region does not exist on the fork; field left zeroed.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/rfnm-vspa.h>

#include "la9310_base.h"
#include <la93xx_ipc_ioctl.h>
#include <la9310_host_if.h>

#include <la9310_modinfo.h>

/* NXP host tools open /dev/shiva<N>; la9310_dev->name here is "nlm<N>" */
#define MODINFO_DEVNAME_PREFIX "shiva"
#define RFNM_LA9310_NAME_PREFIX "nlm"

static int la9310_modinfo_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int rc;
	struct la9310_dev *la9310_dev = filp->private_data;
	phys_addr_t offset = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;
	size_t size = vma->vm_end - vma->vm_start;

	if (!la9310_dev)
		return -EINVAL;

	dev_dbg(la9310_dev->dev, "request to mmap %llx:%lx\n", offset, size);

	if ((offset >= scratch_buf_phys_addr) &&
	    ((offset + size) <= scratch_buf_phys_addr + scratch_buf_size)) {
		vma->vm_page_prot = pgprot_cached(vma->vm_page_prot);
		dev_dbg(la9310_dev->dev,
			"request to mmap %llx:%lx marked cacheable\n",
			offset, size);
	} else
		return -EINVAL;

	rc = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, size,
			     vma->vm_page_prot);
	return rc;
}

void la9310_modinfo_get(struct la9310_dev *la9310_dev, modinfo_t *mi)
{
	int32_t idx;
	struct la9310_mem_region_info *ep_buf;
	int name_len = 2;
	char *brd_name = "NA";
	struct device_node *root;

	memset(mi, 0, sizeof(*mi));

	sprintf(mi->name, "%s", la9310_dev->name);
	mi->id = la9310_dev->id;

	root = of_find_node_by_path("/");
	if (root) {
		if (of_machine_is_compatible("fsl,imx8mp-evk") ||
		    of_machine_is_compatible("fsl,imx8mp")) {
			brd_name = (char *)of_get_property(root, "model",
							   &name_len);
			if (brd_name)
				strncpy(mi->board_name, brd_name, name_len);
		} else {
			strncpy(mi->board_name, brd_name, name_len);
		}
		of_node_put(root);
	} else {
		strncpy(mi->board_name, brd_name, name_len);
	}
	mi->board_name[name_len] = '\0';

	sprintf(mi->pci_addr, "%s", pci_name(la9310_dev->pdev));

	mi->ccsr.host_phy_addr =
		la9310_dev->mem_regions[LA9310_MEM_REGION_CCSR].phys_addr;
	mi->ccsr.size = la9310_dev->mem_regions[LA9310_MEM_REGION_CCSR].size;

	mi->tcml.host_phy_addr =
		la9310_dev->mem_regions[LA9310_MEM_REGION_TCML].phys_addr;
	mi->tcml.size = la9310_dev->mem_regions[LA9310_MEM_REGION_TCML].size;

	mi->tcmu.host_phy_addr =
		la9310_dev->mem_regions[LA9310_MEM_REGION_TCMU].phys_addr;
	mi->tcmu.size = la9310_dev->mem_regions[LA9310_MEM_REGION_TCMU].size;

	mi->pciwin.modem_phy_addr = la9310_dev->pci_outbound_win_start_addr;
	mi->pciwin.size =
		(uint32_t)(la9310_dev->pci_outbound_win_limit -
			   la9310_dev->pci_outbound_win_start_addr);

	idx = LA9310_SUBDRV_DMA_REGION_IDX(LA9310_VSPA_OVERLAY);
	ep_buf = &la9310_dev->dma_info.ep_bufs[idx];
	mi->ov.modem_phy_addr = ep_buf->phys_addr;
	mi->ov.size = ep_buf->size;
	mi->ov.host_phy_addr = scratch_buf_phys_addr + mi->ov.modem_phy_addr
		- mi->pciwin.modem_phy_addr;

	/* VSPA */
	idx = LA9310_SUBDRV_DMA_REGION_IDX(LA9310_MEM_REGION_VSPA);
	ep_buf = &la9310_dev->dma_info.ep_bufs[idx];
	mi->vspa.modem_phy_addr = ep_buf->phys_addr;
	mi->vspa.size = ep_buf->size;
	mi->vspa.host_phy_addr = scratch_buf_phys_addr + mi->vspa.modem_phy_addr
		- mi->pciwin.modem_phy_addr;

	/* FW */
	idx = LA9310_SUBDRV_DMA_REGION_IDX(LA9310_MEM_REGION_FW);
	ep_buf = &la9310_dev->dma_info.ep_bufs[idx];
	mi->fw.modem_phy_addr = ep_buf->phys_addr;
	mi->fw.size = ep_buf->size;
	mi->fw.host_phy_addr = scratch_buf_phys_addr + mi->fw.modem_phy_addr
		- mi->pciwin.modem_phy_addr;

	/* LA9310 LOG buffer */
	idx = LA9310_SUBDRV_DMA_REGION_IDX(LA9310_MEM_REGION_DBG_LOG);
	ep_buf = &la9310_dev->dma_info.ep_bufs[idx];
	mi->dbg.modem_phy_addr = ep_buf->phys_addr;
	mi->dbg.size = ep_buf->size;
	mi->dbg.host_phy_addr = scratch_buf_phys_addr + mi->dbg.modem_phy_addr
		- mi->pciwin.modem_phy_addr;

	/* IQ Data samples */
	idx = LA9310_SUBDRV_DMA_REGION_IDX(LA9310_MEM_REGION_IQ_SAMPLES);
	ep_buf = &la9310_dev->dma_info.ep_bufs[idx];
	mi->iqr.modem_phy_addr = ep_buf->phys_addr;
	mi->iqr.size = ep_buf->size;
	mi->iqr.host_phy_addr = scratch_buf_phys_addr + mi->iqr.modem_phy_addr
		- mi->pciwin.modem_phy_addr;

	/* NLM Operations */
	idx = LA9310_SUBDRV_DMA_REGION_IDX(LA9310_MEM_REGION_NLM_OPS);
	ep_buf = &la9310_dev->dma_info.ep_bufs[idx];
	mi->nlmops.modem_phy_addr = ep_buf->phys_addr;
	mi->nlmops.size = ep_buf->size;
	mi->nlmops.host_phy_addr = scratch_buf_phys_addr +
		mi->nlmops.modem_phy_addr - mi->pciwin.modem_phy_addr;

	idx = LA9310_SUBDRV_DMA_REGION_IDX(LA9310_MEM_REGION_STD_FW);
	ep_buf = &la9310_dev->dma_info.ep_bufs[idx];
	mi->stdfw.modem_phy_addr = ep_buf->phys_addr;
	mi->stdfw.size = ep_buf->size;
	mi->stdfw.host_phy_addr = scratch_buf_phys_addr +
		mi->stdfw.modem_phy_addr - mi->pciwin.modem_phy_addr;

	/* no RF_CAL region on the RFNM fork - mi->rfcal stays zero */

	mi->hif.host_phy_addr =
		la9310_dev->mem_regions[LA9310_MEM_REGION_TCML].phys_addr +
		LA9310_EP_HIF_OFFSET;
	mi->hif.size = sizeof(struct la9310_hif);

	/* Fixed carveout on RFNM: host phys == ATU source address */
	mi->scratchbuf.host_phy_addr = scratch_buf_phys_addr;
	mi->scratchbuf.size = scratch_buf_size;

	mi->dac_mask = dac_mask;
	mi->adc_mask = adc_mask;
	mi->adc_rate_mask = adc_rate_mask;
	mi->dac_rate_mask = dac_rate_mask;

	/*
	 * iqflood as the NXP host tools expect it.
	 *
	 * modem_phy_addr must be the *iqplayer* EP alias (0xB0001000), not
	 * RFNM's 0xC0000000: the host scripts read this base at runtime and
	 * hand it to vspa_mbox (iq-replay.sh:36,51), while only the DMEM-proxy
	 * base is compiled into the VSPA image. Report 0xC0000000 here and
	 * streaming and the proxy end up on two different outbound windows.
	 * la9310_create_iqplayer_iqflood_outbound() creates the matching window.
	 *
	 * size must be the REAL carveout (112 MB), not RFNM_IQFLOOD_MEMSIZE
	 * (208 MB, which deliberately spans iqflood + the iqusb carveout):
	 * lib_iqplayer places the proxy at size-1024 and the RX FIFO at size/2,
	 * so 208 MB lands them inside RFNM's USB buffer and nothing works.
	 */
	mi->iqflood.modem_phy_addr = LA9310_IQPLAYER_IQFLOOD_EP_ADDR;
	mi->iqflood.host_phy_addr = RFNM_IQFLOOD_MEMADDR;
	mi->iqflood.size = LA9310_IQPLAYER_IQFLOOD_SIZE;
}

static long la9310_modinfo_ioctl(struct file *filp, unsigned int cmd,
				 unsigned long arg)
{
	int32_t ret = 0;
	struct la9310_dev *la9310_dev = filp->private_data;
	struct la9310_host_stats *host_stats;
	int list_stats_len = 0, stats_len = 0;
	char *buf;

	switch (cmd) {
	case IOCTL_LA93XX_MODINFO_GET:
	{
		modinfo_t *mil_user = (modinfo_t *)arg;
		modinfo_t mil;
		modinfo_t *mi = &mil;

		ret = copy_from_user(&mil, (modinfo_t *)arg,
				     sizeof(modinfo_t));
		if (ret != 0)
			return -EFAULT;

		la9310_modinfo_get(la9310_dev, mi);

		ret = copy_to_user(mil_user, &mil, sizeof(modinfo_t));
		if (ret != 0)
			return -EFAULT;
	}
	break;
	case IOCTL_LA93XX_MODINFO_GET_STATS:
	{
		modinfo_s *mil_user_s = (modinfo_s *)arg;
		modinfo_s mil_s;
		modinfo_s *mi_s = &mil_s;

		ret = copy_from_user(&mil_s, (modinfo_s *)arg,
				     sizeof(modinfo_s));
		if (ret != 0)
			return -EFAULT;

		buf = mi_s->target_stat;

		list_for_each_entry(host_stats, &la9310_dev->host_stats.list,
				    list) {
			if (stats_len > PAGE_SIZE) {
				dev_err(la9310_dev->dev,
					"LA9310 host stats > sysfs max permisible");
				return stats_len;
			}
			list_stats_len =
				host_stats->stats_ops.la9310_show_stats(
					host_stats->stats_ops.stats_args,
					buf, la9310_dev);
			stats_len += list_stats_len;
			buf += list_stats_len;
		}

		ret = copy_to_user(mil_user_s, &mil_s, sizeof(modinfo_s));
		if (ret != 0)
			return -EFAULT;
	}
	break;
	default:
		dev_err(la9310_dev ? la9310_dev->dev : NULL,
			"INVALID ioctl for la9310modinfo\n");
		return -EINVAL;
	}

	return 0;
}

static int la9310_modinfo_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	return 0;
}

static int la9310_modinfo_open(struct inode *inode, struct file *filp)
{
	struct la9310_dev *la9310_dev;
	const char *file_name = file_dentry(filp)->d_iname;
	char nlm_name[16];
	unsigned int id = 0;

	/* /dev/shiva<N> -> la9310_dev "nlm<N>" */
	if (sscanf(file_name, MODINFO_DEVNAME_PREFIX "%u", &id) != 1)
		return -EINVAL;
	snprintf(nlm_name, sizeof(nlm_name),
		 RFNM_LA9310_NAME_PREFIX "%u", id);

	la9310_dev = get_la9310_dev_byname(nlm_name);
	if (!la9310_dev)
		return -EINVAL;

	filp->private_data = (void *)la9310_dev;

	return 0;
}

static const struct file_operations la9310_modinfo_fops = {
	.owner          = THIS_MODULE,
	.open           = la9310_modinfo_open,
	.release        = la9310_modinfo_release,
	.mmap           = la9310_modinfo_mmap,
	.unlocked_ioctl = la9310_modinfo_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = la9310_modinfo_ioctl,
#endif
};

static struct miscdevice la9310_modinfo_miscdev[MAX_MODEM_INSTANCES];
static char la9310_modinfo_names[MAX_MODEM_INSTANCES][16];

/*
 * Global /sys/shiva/ group with a shiva_status attribute.
 *
 * NXP's driver creates this in la9310_init_global_sysfs(); this fork hangs all
 * of its sysfs under the PCI device (la9310sysfs) and has no global node at
 * all. Every NXP host tool stat()s /sys/shiva/shiva_status before doing
 * anything and exits with "la9310 shiva driver not started" if it is missing
 * (iq_app.c:195) - on an otherwise completely healthy stack. The file is only
 * stat()ed, never parsed, but we report something useful anyway.
 */
static struct kobject *la9310_shiva_kobj;
static struct la9310_dev *la9310_shiva_devs[MAX_MODEM_INSTANCES];

static ssize_t shiva_status_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	int i, len = 0;

	for (i = 0; i < MAX_MODEM_INSTANCES; i++) {
		if (la9310_shiva_devs[i])
			len += scnprintf(buf + len, PAGE_SIZE - len,
					 "modem %d: %s ready\n",
					 i, la9310_shiva_devs[i]->name);
	}
	if (!len)
		len = scnprintf(buf, PAGE_SIZE, "no modem\n");

	return len;
}

static struct kobj_attribute shiva_status_attr =
	__ATTR(shiva_status, 0444, shiva_status_show, NULL);

static struct attribute *la9310_shiva_attrs[] = {
	&shiva_status_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(la9310_shiva);

static int la9310_shiva_sysfs_add(struct la9310_dev *la9310_dev)
{
	int rc;

	if (!la9310_shiva_kobj) {
		la9310_shiva_kobj = kobject_create_and_add("shiva", NULL);
		if (!la9310_shiva_kobj)
			return -ENOMEM;

		rc = sysfs_create_groups(la9310_shiva_kobj,
					 la9310_shiva_groups);
		if (rc) {
			kobject_put(la9310_shiva_kobj);
			la9310_shiva_kobj = NULL;
			return rc;
		}
		dev_info(la9310_dev->dev, "la9310_modinfo: /sys/shiva created\n");
	}

	la9310_shiva_devs[la9310_dev->id] = la9310_dev;
	return 0;
}

static void la9310_shiva_sysfs_del(struct la9310_dev *la9310_dev)
{
	int i;

	la9310_shiva_devs[la9310_dev->id] = NULL;

	for (i = 0; i < MAX_MODEM_INSTANCES; i++)
		if (la9310_shiva_devs[i])
			return;		/* another modem still present */

	if (la9310_shiva_kobj) {
		sysfs_remove_groups(la9310_shiva_kobj, la9310_shiva_groups);
		kobject_put(la9310_shiva_kobj);
		la9310_shiva_kobj = NULL;
	}
}

int la9310_modinfo_init(struct la9310_dev *la9310_dev)
{
	int rc = -1;
	struct miscdevice *miscdev;

	if (la9310_dev->id >= MAX_MODEM_INSTANCES) {
		dev_err(la9310_dev->dev, "Invalid modem id : %d\n",
			la9310_dev->id);
		return rc;
	}

	miscdev = &la9310_modinfo_miscdev[la9310_dev->id];

	snprintf(la9310_modinfo_names[la9310_dev->id],
		 sizeof(la9310_modinfo_names[0]),
		 MODINFO_DEVNAME_PREFIX "%d", la9310_dev->id);
	miscdev->name = la9310_modinfo_names[la9310_dev->id];
	miscdev->fops = &la9310_modinfo_fops;
	miscdev->minor = MISC_DYNAMIC_MINOR;

	rc = misc_register(miscdev);

	if (rc) {
		dev_err(la9310_dev->dev,
			"la9310_modinfo: failed to register misc device\n");
		return rc;
	}

	dev_info(la9310_dev->dev,
		 "la9310_modinfo: /dev/%s ready (minor %d)\n",
		 miscdev->name, miscdev->minor);

	/* NXP host tools refuse to start without this - see comment above */
	if (la9310_shiva_sysfs_add(la9310_dev))
		dev_warn(la9310_dev->dev,
			 "la9310_modinfo: /sys/shiva not created - NXP host tools will refuse to start\n");

	return 0;
}
EXPORT_SYMBOL_GPL(la9310_modinfo_init);

int la9310_modinfo_exit(struct la9310_dev *la9310_dev)
{
	if (la9310_dev->id >= MAX_MODEM_INSTANCES) {
		dev_err(la9310_dev->dev, "Invalid modem id : %d\n",
			la9310_dev->id);
		return -1;
	}
	la9310_shiva_sysfs_del(la9310_dev);
	if (la9310_modinfo_miscdev[la9310_dev->id].fops)
		misc_deregister(&la9310_modinfo_miscdev[la9310_dev->id]);
	return 0;
}
EXPORT_SYMBOL_GPL(la9310_modinfo_exit);
