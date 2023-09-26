/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2017-2021 NXP
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/of_device.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/version.h>

#include "la9310_host_if.h"
#include "la9310_base.h"
#include "la9310_vspa.h"

static uint32_t iq_samples_cnt;

static ssize_t
la9310_collect_ep_log(struct la9310_ep_log *ep_log, char *buf)
{
	int log_len, max_len, str_len;
	char *ep_log_str;

	log_len = 0;
	str_len = 0;
	max_len = 0;

	ep_log_str = ep_log->buf + ep_log->offset;
	max_len = ep_log->len - ep_log->offset;
	str_len = strnlen(ep_log_str, max_len);
	if (str_len) {
		memcpy(buf, ep_log_str, str_len);
		memset_io(ep_log_str, 0, str_len);
		ep_log->offset += str_len;
		if (ep_log->offset >= ep_log->len)
			ep_log->offset = 0;
		log_len += str_len;
		buf += str_len;

		if (max_len == str_len) {
			ep_log_str = ep_log->buf;
			str_len = strlen(ep_log_str);
			if (str_len) {
				memcpy(buf, ep_log_str, str_len);
				memset_io(ep_log_str, 0, str_len);
				log_len += str_len;
				buf += str_len;
			}
			ep_log->offset = str_len;
			if (ep_log->offset >= ep_log->len)
				ep_log->offset = 0;
		}
	}

	return log_len;
}

static ssize_t
la9310_show_ep_log(struct device *dev,
		   struct device_attribute *attr, char *buf)
{
	struct la9310_dev *la9310_dev;
	struct la9310_ep_log *ep_log;
	int log_len, i;
	dma_addr_t dma_addr;

	la9310_dev = dev_get_drvdata(dev);
	ep_log = &la9310_dev->ep_log;
	log_len = 0;

	dev_info(la9310_dev->dev,
		 "LA9310 log buf dump, vaddr %px, offset %d\n", ep_log->buf,
		 ep_log->offset);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0)
        dma_addr=dma_map_page_attrs(&la9310_dev->pdev->dev,
                        virt_to_page(ep_log->buf),
                        offset_in_page(ep_log->buf), ep_log->len,
                        (enum dma_data_direction)PCI_DMA_TODEVICE, 0);
	 dev_info(la9310_dev->dev,"### dep_log->buf %px dma_addr %px\n",ep_log->buf, (void*)dma_addr);
#else
  	pci_map_single(la9310_dev->pdev, ep_log->buf, ep_log->len,
		       PCI_DMA_FROMDEVICE);
#endif
	log_len = la9310_collect_ep_log(ep_log, buf);
	if (log_len == 0) {
		for (i = 0; i < ep_log->len; i++) {
			if (ep_log->buf[i] != 0) {
				ep_log->offset = i;
				log_len = la9310_collect_ep_log(ep_log, buf);
			}
		}
	}

	dev_info(la9310_dev->dev, "log len: %d, offset : %d\n", log_len,
		 ep_log->offset);

	return log_len;
}

static ssize_t
la9310_reset_ep_log(struct device *dev,
		    struct device_attribute *attr, const char *buf,
		    size_t count)
{

	struct la9310_dev *la9310_dev;
	int rc = 0;
	unsigned long val;
	struct la9310_ep_log *ep_log;

	la9310_dev = dev_get_drvdata(dev);
	ep_log = &la9310_dev->ep_log;

	rc = kstrtoul(buf, 0, &val);
	if (rc) {
		dev_err(la9310_dev->dev, "%s is not hex or decimal\n", buf);
		goto out;
	}

	if (val) {
		dev_err(la9310_dev->dev,
			"%d not valid. Write 0 to reset buffer\n", (int) val);
		goto out;
	}

	/* reset LA9310 End-Point debug log buffer */
	memset_io(ep_log->buf, 0, ep_log->len);
	ep_log->offset = 0;

	dev_info(la9310_dev->dev,
		 "LA9310 log buf reset, vaddr %p, offset %d\n", ep_log->buf,
		 ep_log->offset);
out:
	return strnlen(buf, count);
}

int
la9310_host_add_stats(struct la9310_dev *la9310_dev,
		      struct la9310_stats_ops *stats_ops)
{
	struct la9310_host_stats *host_stats;

	if (!stats_ops) {
		dev_err(la9310_dev->dev, "LA9310 add stats");
		return -EINVAL;
	}
	host_stats = kzalloc(sizeof(struct la9310_host_stats), GFP_KERNEL);
	if (host_stats == NULL)
		return -ENOMEM;
	memcpy(&host_stats->stats_ops, stats_ops,
	       sizeof(struct la9310_stats_ops));
	list_add_tail(&host_stats->list, &la9310_dev->host_stats.list);
	return 0;
}

static ssize_t
la9310_show_stats(struct device *dev,
		  struct device_attribute *attr, char *buf)
{
	struct la9310_dev *la9310_dev;
	struct la9310_host_stats *host_stats;
	int list_stats_len = 0, stats_len = 0;

	la9310_dev = dev_get_drvdata(dev);
	host_stats = &la9310_dev->host_stats;

	dev_info(la9310_dev->dev, "LA9310 host stats dump\n");

	list_for_each_entry(host_stats, &la9310_dev->host_stats.list, list) {
		if (stats_len > PAGE_SIZE) {
			dev_err(la9310_dev->dev,
				"LA9310 host stats > sysfs max permisible");
			return stats_len;
		}
		list_stats_len =
			host_stats->stats_ops.la9310_show_stats(host_stats->
								stats_ops.
								stats_args,
								buf,
								la9310_dev);
		stats_len += list_stats_len;
		buf += list_stats_len;
	}
	return stats_len;
}

static ssize_t
la9310_reset_stats(struct device *dev,
		   struct device_attribute *attr, const char *buf,
		   size_t count)
{
	struct la9310_dev *la9310_dev;
	struct la9310_host_stats *host_stats;
	int rc;
	unsigned long val;

	la9310_dev = dev_get_drvdata(dev);
	rc = kstrtoul(buf, 0, &val);
	if (rc) {
		dev_err(la9310_dev->dev, "%s is not hex or decimal\n", buf);
		goto out;
	}

	if (val) {
		dev_err(la9310_dev->dev,
			"%d not valid. Write 0 to reset buffer\n", (int) val);
		goto out;
	}

	host_stats = &la9310_dev->host_stats;

	dev_info(la9310_dev->dev, "LA9310 host reset stats\n");

	list_for_each_entry(host_stats, &la9310_dev->host_stats.list, list)
		host_stats->stats_ops.la9310_reset_stats(host_stats->
							 stats_ops.
							 stats_args);
out:
	return strnlen(buf, count);
}

static void
sysfs_del_host_stats_list(struct la9310_dev *la9310_dev)
{
	struct la9310_host_stats *host_stats, *host_stats_tmp;

	host_stats = &la9310_dev->host_stats;
	dev_info(la9310_dev->dev, "LA9310 host stats list delete\n");

	list_for_each_entry_safe(host_stats, host_stats_tmp,
				 &la9310_dev->host_stats.list, list) {
		list_del(&host_stats->list);
		kfree(host_stats);
	}
}

static ssize_t
la9310_show_ep_log_level(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct la9310_dev *la9310_dev;
	struct debug_log_regs *dbg_log_regs;

	la9310_dev = dev_get_drvdata(dev);
	dbg_log_regs = &la9310_dev->hif->dbg_log_regs;
	return snprintf(buf, LA9310_DBG_LOG_MAX_STRLEN,
			"LA9310 log level - %d\n",
			readl(&dbg_log_regs->log_level));
}

static ssize_t
la9310_set_ep_log_level(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{

	struct la9310_dev *la9310_dev;
	struct debug_log_regs *dbg_log_regs;
	int rc = 0;
	unsigned long val;

	la9310_dev = dev_get_drvdata(dev);

	rc = kstrtoul(buf, 0, &val);
	if (rc) {
		dev_err(la9310_dev->dev, "%s is not hex or decimal\n", buf);
		goto out;
	}
	if ((val < LA9310_LOG_LEVEL_ERR) || (val > LA9310_LOG_LEVEL_ALL)) {
		dev_err(la9310_dev->dev,
			"Invalid level %d, valid [%d - %d]\n", (int) val,
			LA9310_LOG_LEVEL_ERR, LA9310_LOG_LEVEL_ALL);
		goto out;
	}

	dbg_log_regs = &la9310_dev->hif->dbg_log_regs;
	writel(val, &dbg_log_regs->log_level);
out:
	return strnlen(buf, count);
}

static ssize_t
la9310_show_stats_control_mask(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct la9310_dev *la9310_dev;

	la9310_dev = dev_get_drvdata(dev);
	return sprintf(buf, "%x\n", la9310_dev->stats_control);
}

static ssize_t
la9310_set_stats_control_mask(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{

	struct la9310_dev *la9310_dev;
	int rc = 0;
	unsigned long val;

	la9310_dev = dev_get_drvdata(dev);

	rc = kstrtoul(buf, 0, &val);
	if (rc) {
		dev_err(la9310_dev->dev, "%s is not hex or decimal\n", buf);
		goto out;
	} else {
		la9310_dev->stats_control = val;
	}
out:
	return strnlen(buf, count);
}

static ssize_t
show_vspa_info(struct device *dev, struct device_attribute *attr, char *buf)
{
	int len = 0;
	struct la9310_dev *la9310_dev = dev_get_drvdata(dev);
	struct vspa_device *vspadev = (struct vspa_device *)
		la9310_dev->vspa_priv;
	u32 __iomem *regs = vspadev->regs;

	len += sprintf(buf, "VSPA Binary Software Version = \t%x\n",
		       vspa_reg_read(regs + SWVERSION_REG_OFFSET));
	len += sprintf((len + buf), "VSPA state = \t%d\n",
		       full_state(vspadev));

	return len;
}

static ssize_t
vspa_overlay_get_function(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct la9310_dev *la9310_dev = dev_get_drvdata(dev);
	struct vspa_device *vspadev = (struct vspa_device *)
		la9310_dev->vspa_priv;
	return sprintf(buf, "Currently loaded overlay section: %s\n",
		       vspadev->overlay_sec_loaded);
}

static ssize_t
vspa_overlay_set_function(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct la9310_dev *la9310_dev = dev_get_drvdata(dev);
	struct vspa_device *vspadev = (struct vspa_device *)
		la9310_dev->vspa_priv;
	char param1[MAX_SECTION_NAME];
	int i = 0, ret = 0, retval;

	if (count > MAX_SECTION_NAME) {
		dev_err(la9310_dev->dev, "Section Name is too big\n");
		return -EINVAL;
	}

	retval = sscanf(buf, "%s", param1);

	for (i = 0; i < MAX_OVERLAY_SECTIONS; i++) {
		if (!strncmp(vspadev->overlay_sec[i].name, buf,
			     strlen(vspadev->overlay_sec[i].name))) {
			dev_dbg(la9310_dev->dev,
				"Overlay section identified: %s\n",
				vspadev->overlay_sec[i].name);

			/*Initiating DMA for overlay section */
			ret = overlay_initiate(dev, vspadev->overlay_sec[i]);
			if (ret < 0) {
				dev_err(la9310_dev->dev,
					"VSPA Overlay Failed to load\n");
				return ret;
			}
		}
	}

	return strnlen(buf, count);
}

static ssize_t
la9310_iq_samples_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{

	struct la9310_dev *la9310_dev;
	struct la9310_mem_region_info *iq_samples_region;

	la9310_dev = dev_get_drvdata(dev);
	iq_samples_region = la9310_get_dma_region(la9310_dev,
			LA9310_MEM_REGION_IQ_SAMPLES);
	la9310_hexdump(iq_samples_region->vaddr, iq_samples_cnt);

	return iq_samples_cnt;
}

static ssize_t
la9310_iq_samples_size(struct device *dev,
	struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct la9310_dev *la9310_dev;
	int ret;

	la9310_dev = dev_get_drvdata(dev);

	ret = kstrtou32(buf, 0, &iq_samples_cnt);

	if (!ret) {

		if (iq_samples_cnt  <= LA9310_IQ_SAMPLES_SIZE) {
			dev_info(la9310_dev->dev, "Set IQ Samples size %d to dump\n",
					iq_samples_cnt);
		} else {
			dev_info(la9310_dev->dev, "Invalid IQ Samples size %d\n",
					iq_samples_cnt);
			iq_samples_cnt = 0;
		}
		return iq_samples_cnt;
	} else
		return 0;
}

static DEVICE_ATTR(target_log, S_IWUSR | S_IRUGO,
		   la9310_show_ep_log, la9310_reset_ep_log);
static DEVICE_ATTR(target_stats, S_IWUSR | S_IRUGO,
		   la9310_show_stats, la9310_reset_stats);
static DEVICE_ATTR(target_log_level, S_IWUSR | S_IRUGO,
		   la9310_show_ep_log_level, la9310_set_ep_log_level);
static DEVICE_ATTR(target_stats_control, S_IWUSR | S_IRUGO,
		   la9310_show_stats_control_mask,
		   la9310_set_stats_control_mask);
static DEVICE_ATTR(vspa_info, S_IRUGO, show_vspa_info, NULL);
static DEVICE_ATTR(vspa_do_overlay, S_IWUSR | S_IRUGO,
		   vspa_overlay_get_function, vspa_overlay_set_function);
static DEVICE_ATTR(iq_samples, S_IWUSR | S_IRUGO,
		   la9310_iq_samples_show, la9310_iq_samples_size);

static struct attribute *la9310_sysfs_entries[] = {
	&dev_attr_target_log.attr,
	&dev_attr_target_log_level.attr,
	&dev_attr_target_stats_control.attr,
	&dev_attr_target_stats.attr,
	&dev_attr_vspa_info.attr,
	&dev_attr_vspa_do_overlay.attr,
	&dev_attr_iq_samples.attr,
	NULL
};

/* "name" is folder name witch will be
 * put in device directory like :
 * sys/devices/pci0000:00/0000:00:1c.4/
 * 0000:06:00.0/la9310sysfs
 */
struct attribute_group la9310_attribute_group = {
	.name = "la9310sysfs",
	.attrs = la9310_sysfs_entries,
};

int
la9310_init_sysfs(struct la9310_dev *la9310_dev)
{
	int rc = 0;

	/*XXX:FIXME when WLAN registration is done. Remove
	 * dev_set_drvdata(), get it through: struct ieee80211_hw *hw =
	 * dev_get_drvdata(d);
	 */
	dev_set_drvdata(la9310_dev->dev, la9310_dev);

	rc = sysfs_create_group(&la9310_dev->pdev->dev.kobj,
				&la9310_attribute_group);
	if (rc) {
		dev_err(la9310_dev->dev, "Failed to create sysfs group\n");
		goto out;
	}
	INIT_LIST_HEAD(&la9310_dev->host_stats.list);
	dev_info(la9310_dev->dev, "Created sysfs group %s\n",
		 la9310_attribute_group.name);
out:
	return rc;
}

void
la9310_remove_sysfs(struct la9310_dev *la9310_dev)
{
	sysfs_remove_group(&la9310_dev->pdev->dev.kobj,
			   &la9310_attribute_group);
}

void
la9310_remove_stats (struct la9310_dev *la9310_dev)
{
	sysfs_del_host_stats_list(la9310_dev);
}
