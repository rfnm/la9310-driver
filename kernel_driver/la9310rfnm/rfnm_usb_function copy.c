// SPDX-License-Identifier: GPL-2.0+
/*
 * f_sourcesink.c - USB peripheral source/sink configuration driver
 *
 * Copyright (C) 2003-2008 David Brownell
 * Copyright (C) 2008 by Nokia Corporation
 */

/* #define VERBOSE_DEBUG */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/usb/composite.h>
#include <linux/err.h>

#include "/home/davide/imx-rfnm-bsp/build/tmp/work-shared/imx8mp-rfnm/kernel-source/drivers/usb/gadget/function/g_zero.h"
#include "/home/davide/imx-rfnm-bsp/build/tmp/work-shared/imx8mp-rfnm/kernel-source/drivers/usb/gadget/u_f.h"

#define RFNM_EP_CNT 4

struct f_sourcesink {
	struct usb_function	function;

	struct usb_ep		*in_ep[RFNM_EP_CNT];
	struct usb_ep		*out_ep[RFNM_EP_CNT];
	int			cur_alt;

	unsigned pattern;
	unsigned buflen;
	unsigned bulk_qlen;
	unsigned iso_qlen;
};

static inline struct f_sourcesink *func_to_ss(struct usb_function *f)
{
	return container_of(f, struct f_sourcesink, function);
}

/*-------------------------------------------------------------------------*/

static struct usb_interface_descriptor source_sink_intf_alt0 = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bAlternateSetting =	0,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	/* .iInterface		= DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor fs_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *fs_source_sink_descs[] = {
	NULL,
};

/* high speed support: */

static struct usb_descriptor_header *hs_source_sink_descs[] = {
	NULL,
};

/* super speed support: */

static struct usb_endpoint_descriptor ss_source_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_source_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	0,
};

static struct usb_endpoint_descriptor ss_sink_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_sink_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	0,
};


static struct usb_descriptor_header *ss_source_sink_descs[] = {
	(struct usb_descriptor_header *) &source_sink_intf_alt0,
	(struct usb_descriptor_header *) &ss_source_desc,
	(struct usb_descriptor_header *) &ss_source_comp_desc,
	(struct usb_descriptor_header *) &ss_sink_desc,
	(struct usb_descriptor_header *) &ss_sink_comp_desc,
	NULL,
};

/* function-specific strings: */

static struct usb_string strings_sourcesink[] = {
	[0].s = "source and sink data",
	{  }			/* end of list */
};

static struct usb_gadget_strings stringtab_sourcesink = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_sourcesink,
};

static struct usb_gadget_strings *sourcesink_strings[] = {
	&stringtab_sourcesink,
	NULL,
};

/*-------------------------------------------------------------------------*/

static inline struct usb_request *ss_alloc_ep_req(struct usb_ep *ep, int len)
{
	return alloc_ep_req(ep, len);
}

static void disable_ep(struct usb_composite_dev *cdev, struct usb_ep *ep)
{
	int			value;

	value = usb_ep_disable(ep);
	if (value < 0)
		DBG(cdev, "disable %s --> %d\n", ep->name, value);
}


struct usb_os_desc	rfnm_os_desc;

static int
sourcesink_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_sourcesink	*ss = func_to_ss(f);
	int	id, i;
	int ret;

	if (cdev->use_os_string) {
		printk("allocating table from function file\n");
		f->os_desc_table = kzalloc(sizeof(*f->os_desc_table), GFP_KERNEL);

		rfnm_os_desc.ext_compat_id = kzalloc(20, GFP_KERNEL);
		strcpy(rfnm_os_desc.ext_compat_id, "WINUSB");
		INIT_LIST_HEAD(&rfnm_os_desc.ext_prop);

		if (!f->os_desc_table)
			return -ENOMEM;
		f->os_desc_n = 1;
		f->os_desc_table[0].os_desc = &rfnm_os_desc;
		//f->os_desc_table[0].if_id = ncm_iad_desc.bFirstInterface;
	}



	/* allocate interface ID(s) */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	source_sink_intf_alt0.bInterfaceNumber = id;

	for(i = 0; i < RFNM_EP_CNT; i++) {
		ss->in_ep[i] = usb_ep_autoconfig(cdev->gadget, &fs_source_desc);
		if (!ss->in_ep[i])
			goto autoconf_fail;

		ss->out_ep[i] = usb_ep_autoconfig(cdev->gadget, &fs_sink_desc);
		if (!ss->out_ep[i])
			goto autoconf_fail;
	}
	

	

	/* support super speed hardware */
	ss_source_desc.bEndpointAddress = fs_source_desc.bEndpointAddress;
	ss_sink_desc.bEndpointAddress = fs_sink_desc.bEndpointAddress;

	ret = usb_assign_descriptors(f, fs_source_sink_descs,
			hs_source_sink_descs, ss_source_sink_descs,
			ss_source_sink_descs);
	if (ret)
		return ret;

	return 0;

autoconf_fail:
	ERROR(cdev, "%s: can't autoconfigure on %s\n",
		f->name, cdev->gadget->name);
	return -ENODEV;
}

static void
sourcesink_free_func(struct usb_function *f)
{
	struct f_ss_opts *opts;

	opts = container_of(f->fi, struct f_ss_opts, func_inst);

	mutex_lock(&opts->lock);
	opts->refcnt--;
	mutex_unlock(&opts->lock);

	usb_free_all_descriptors(f);
	kfree(func_to_ss(f));
}

/* optionally require specific source/sink data patterns  */
static int check_read_data(struct f_sourcesink *ss, struct usb_request *req)
{
	unsigned		i;
	u8			*buf = req->buf;
	struct usb_composite_dev *cdev = ss->function.config->cdev;
	int max_packet_size = 1024;

	if (ss->pattern == 2)
		return 0;

	for (i = 0; i < req->actual; i++, buf++) {
		switch (ss->pattern) {

		/* all-zeroes has no synchronization issues */
		case 0:
			if (*buf == 0)
				continue;
			break;

		/* "mod63" stays in sync with short-terminated transfers,
		 * OR otherwise when host and gadget agree on how large
		 * each usb transfer request should be.  Resync is done
		 * with set_interface or set_config.  (We *WANT* it to
		 * get quickly out of sync if controllers or their drivers
		 * stutter for any reason, including buffer duplication...)
		 */
		case 1:
			if (*buf == (u8)((i % max_packet_size) % 63))
				continue;
			break;
		}
		ERROR(cdev, "bad OUT byte, buf[%d] = %d\n", i, *buf);
		usb_ep_set_halt(ss->out_ep[0]);
		return -EINVAL;
	}
	return 0;
}

static void reinit_write_data(struct usb_ep *ep, struct usb_request *req)
{
	unsigned	i;
	u8		*buf = req->buf;
	int max_packet_size = le16_to_cpu(ep->desc->wMaxPacketSize);
	struct f_sourcesink *ss = ep->driver_data;

	switch (ss->pattern) {
	case 0:
		memset(req->buf, 0, req->length);
		break;
	case 1:
		for  (i = 0; i < req->length; i++)
			*buf++ = (u8) ((i % max_packet_size) % 63);
		break;
	case 2:
		break;
	}
}

static void source_sink_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct usb_composite_dev	*cdev;
	struct f_sourcesink		*ss = ep->driver_data;
	int				status = req->status;

	/* driver_data will be null if ep has been disabled */
	if (!ss)
		return;

	cdev = ss->function.config->cdev;

	switch (status) {

	case 0:				/* normal completion? */
		if (ep == ss->out_ep[0]) {
			check_read_data(ss, req);
			if (ss->pattern != 2)
				memset(req->buf, 0x55, req->length);
		}
		break;

	/* this endpoint is normally active while we're configured */
	case -ECONNABORTED:		/* hardware forced ep reset */
	case -ECONNRESET:		/* request dequeued */
	case -ESHUTDOWN:		/* disconnect from host */
		VDBG(cdev, "%s gone (%d), %d/%d\n", ep->name, status,
				req->actual, req->length);
		if (ep == ss->out_ep[0])
			check_read_data(ss, req);
		free_ep_req(ep, req);
		return;

	case -EOVERFLOW:		/* buffer overrun on read means that
					 * we didn't provide a big enough
					 * buffer.
					 */
	default:
#if 1
		DBG(cdev, "%s complete --> %d, %d/%d\n", ep->name,
				status, req->actual, req->length);
		break;
#endif
	case -EREMOTEIO:		/* short read */
		break;
	}

	status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		ERROR(cdev, "kill %s:  resubmit %d bytes --> %d\n",
				ep->name, req->length, status);
		usb_ep_set_halt(ep);
		/* FIXME recover later ... somehow */
	}
}

static int source_sink_start_ep(struct f_sourcesink *ss, bool is_in,
		bool is_iso, int speed)
{
	struct usb_ep		*ep;
	struct usb_request	*req;
	int			i, size, qlen, status = 0;

	if (is_iso) {
		
	} else {
		ep = is_in ? ss->in_ep[0] : ss->out_ep[0];
		qlen = ss->bulk_qlen;
		size = ss->buflen;
	}

	for (i = 0; i < qlen; i++) {
		req = ss_alloc_ep_req(ep, size);
		if (!req)
			return -ENOMEM;

		req->complete = source_sink_complete;
		if (is_in)
			reinit_write_data(ep, req);
		else if (ss->pattern != 2)
			memset(req->buf, 0x55, req->length);

		status = usb_ep_queue(ep, req, GFP_ATOMIC);
		if (status) {
			struct usb_composite_dev	*cdev;

			cdev = ss->function.config->cdev;
			ERROR(cdev, "start %s%s %s --> %d\n",
			      is_iso ? "ISO-" : "", is_in ? "IN" : "OUT",
			      ep->name, status);
			free_ep_req(ep, req);
			return status;
		}
	}

	return status;
}

static void disable_source_sink(struct f_sourcesink *ss)
{
	struct usb_composite_dev	*cdev;

	cdev = ss->function.config->cdev;
	disable_ep(cdev, ss->in_ep[0]);
	disable_ep(cdev, ss->out_ep[0]);
	VDBG(cdev, "%s disabled\n", ss->function.name);
}

static int
enable_source_sink(struct usb_composite_dev *cdev, struct f_sourcesink *ss,
		int alt)
{
	int					result = 0;
	int					speed = cdev->gadget->speed;
	struct usb_ep				*ep;

	/* one bulk endpoint writes (sources) zeroes IN (to the host) */
	ep = ss->in_ep[0];
	result = config_ep_by_speed(cdev->gadget, &(ss->function), ep);
	if (result)
		return result;
	result = usb_ep_enable(ep);
	if (result < 0)
		return result;
	ep->driver_data = ss;

	result = source_sink_start_ep(ss, true, false, speed);
	if (result < 0) {
fail:
		ep = ss->in_ep[0];
		usb_ep_disable(ep);
		return result;
	}

	/* one bulk endpoint reads (sinks) anything OUT (from the host) */
	ep = ss->out_ep[0];
	result = config_ep_by_speed(cdev->gadget, &(ss->function), ep);
	if (result)
		goto fail;
	result = usb_ep_enable(ep);
	if (result < 0)
		goto fail;
	ep->driver_data = ss;

	result = source_sink_start_ep(ss, false, false, speed);
	if (result < 0) {
fail2:
		ep = ss->out_ep[0];
		usb_ep_disable(ep);
		goto fail;
	}

	if (alt == 0)
		goto out;
out:
	ss->cur_alt = alt;

	DBG(cdev, "%s enabled, alt intf %d\n", ss->function.name, alt);
	return result;
}

static int sourcesink_set_alt(struct usb_function *f,
		unsigned intf, unsigned alt)
{
	struct f_sourcesink		*ss = func_to_ss(f);
	struct usb_composite_dev	*cdev = f->config->cdev;

	disable_source_sink(ss);
	return enable_source_sink(cdev, ss, alt);
}

static int sourcesink_get_alt(struct usb_function *f, unsigned intf)
{
	struct f_sourcesink		*ss = func_to_ss(f);

	return ss->cur_alt;
}

static void sourcesink_disable(struct usb_function *f)
{
	struct f_sourcesink	*ss = func_to_ss(f);

	disable_source_sink(ss);
}

/*-------------------------------------------------------------------------*/

static int sourcesink_setup(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct usb_configuration        *c = f->config;
	struct usb_request	*req = c->cdev->req;
	int			value = -EOPNOTSUPP;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	req->length = USB_COMP_EP0_BUFSIZ;

	/* composite driver infrastructure handles everything except
	 * the two control test requests.
	 */
	switch (ctrl->bRequest) {

	/*
	 * These are the same vendor-specific requests supported by
	 * Intel's USB 2.0 compliance test devices.  We exceed that
	 * device spec by allowing multiple-packet requests.
	 *
	 * NOTE:  the Control-OUT data stays in req->buf ... better
	 * would be copying it into a scratch buffer, so that other
	 * requests may safely intervene.
	 */
	case 0x5b:	/* control WRITE test -- fill the buffer */
		if (ctrl->bRequestType != (USB_DIR_OUT|USB_TYPE_VENDOR))
			goto unknown;
		if (w_value || w_index)
			break;
		/* just read that many bytes into the buffer */
		if (w_length > req->length)
			break;
		value = w_length;
		break;
	case 0x5c:	/* control READ test -- return the buffer */
		if (ctrl->bRequestType != (USB_DIR_IN|USB_TYPE_VENDOR))
			goto unknown;
		if (w_value || w_index)
			break;
		/* expect those bytes are still in the buffer; send back */
		if (w_length > req->length)
			break;
		value = w_length;
		break;

	default:
unknown:
		VDBG(c->cdev,
			"unknown control req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
	}

	/* respond with data transfer or status phase? */
	if (value >= 0) {
		VDBG(c->cdev, "source/sink req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
		req->zero = 0;
		req->length = value;
		value = usb_ep_queue(c->cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0)
			ERROR(c->cdev, "source/sink response, err %d\n",
					value);
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}

static struct usb_function *source_sink_alloc_func(
		struct usb_function_instance *fi)
{
	struct f_sourcesink     *ss;
	struct f_ss_opts	*ss_opts;

	ss = kzalloc(sizeof(*ss), GFP_KERNEL);
	if (!ss)
		return ERR_PTR(-ENOMEM);

	ss_opts =  container_of(fi, struct f_ss_opts, func_inst);

	mutex_lock(&ss_opts->lock);
	ss_opts->refcnt++;
	mutex_unlock(&ss_opts->lock);

	ss->pattern = ss_opts->pattern;
	ss->buflen = ss_opts->bulk_buflen;
	ss->bulk_qlen = ss_opts->bulk_qlen;
	ss->iso_qlen = ss_opts->iso_qlen;

	ss->function.name = "source/sink";
	ss->function.bind = sourcesink_bind;
	ss->function.set_alt = sourcesink_set_alt;
	ss->function.get_alt = sourcesink_get_alt;
	ss->function.disable = sourcesink_disable;
	ss->function.setup = sourcesink_setup;
	ss->function.strings = sourcesink_strings;

	ss->function.free_func = sourcesink_free_func;

	return &ss->function;
}

static inline struct f_ss_opts *to_f_ss_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_ss_opts,
			    func_inst.group);
}

static void ss_attr_release(struct config_item *item)
{
	struct f_ss_opts *ss_opts = to_f_ss_opts(item);

	usb_put_function_instance(&ss_opts->func_inst);
}

static struct configfs_item_operations ss_item_ops = {
	.release		= ss_attr_release,
};



static struct configfs_attribute *ss_attrs[] = {
	NULL,
};

static const struct config_item_type ss_func_type = {
	.ct_item_ops    = &ss_item_ops,
	.ct_attrs	= ss_attrs,
	.ct_owner       = THIS_MODULE,
};

static void source_sink_free_instance(struct usb_function_instance *fi)
{
	struct f_ss_opts *ss_opts;

	ss_opts = container_of(fi, struct f_ss_opts, func_inst);
	kfree(ss_opts);
}

static struct usb_function_instance *source_sink_alloc_inst(void)
{
	struct f_ss_opts *ss_opts;

	ss_opts = kzalloc(sizeof(*ss_opts), GFP_KERNEL);
	if (!ss_opts)
		return ERR_PTR(-ENOMEM);
	mutex_init(&ss_opts->lock);
	ss_opts->func_inst.free_func_inst = source_sink_free_instance;
	ss_opts->isoc_interval = GZERO_ISOC_INTERVAL;
	ss_opts->isoc_maxpacket = GZERO_ISOC_MAXPACKET;
	ss_opts->bulk_buflen = GZERO_BULK_BUFLEN;
	ss_opts->bulk_qlen = GZERO_SS_BULK_QLEN;
	ss_opts->iso_qlen = GZERO_SS_ISO_QLEN;

	config_group_init_type_name(&ss_opts->func_inst.group, "",
				    &ss_func_type);

	return &ss_opts->func_inst;
}
DECLARE_USB_FUNCTION(RFNM, source_sink_alloc_inst,
		source_sink_alloc_func);

static int __init sslb_modinit(void)
{
	int ret;

	ret = usb_function_register(&RFNMusb_func);
	if (ret)
		return ret;
	if (ret)
		usb_function_unregister(&RFNMusb_func);
	return ret;
}
static void __exit sslb_modexit(void)
{
	usb_function_unregister(&RFNMusb_func);
}
module_init(sslb_modinit);
module_exit(sslb_modexit);

MODULE_LICENSE("GPL");