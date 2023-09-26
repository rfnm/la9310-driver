// SPDX-License-Identifier: GPL-2.0+
/*
 * mass_storage.c -- Mass Storage USB Gadget
 *
 * Copyright (C) 2003-2008 Alan Stern
 * Copyright (C) 2009 Samsung Electronics
 *                    Author: Michal Nazarewicz <mina86@mina86.com>
 * All rights reserved.
 */


/*
 * The Mass Storage Gadget acts as a USB Mass Storage device,
 * appearing to the host as a disk drive or as a CD-ROM drive.  In
 * addition to providing an example of a genuinely useful gadget
 * driver for a USB device, it also illustrates a technique of
 * double-buffering for increased throughput.  Last but not least, it
 * gives an easy way to probe the behavior of the Mass Storage drivers
 * in a USB host.
 *
 * Since this file serves only administrative purposes and all the
 * business logic is implemented in f_mass_storage.* file.  Read
 * comments in this file for more detailed description.
 */

#include <linux/kernel.h>
#include <linux/usb/ch9.h>
#include <linux/module.h>

#include "/home/davide/imx-rfnm-bsp/build/tmp/work-shared/imx8mp-rfnm/kernel-source/drivers/usb/gadget/function/f_mass_storage.h"

/*-------------------------------------------------------------------------*/
USB_GADGET_COMPOSITE_OPTIONS();

static struct usb_device_descriptor msg_device_desc = {
	.bLength =		sizeof msg_device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	/* .bcdUSB = DYNAMIC */
	.bDeviceClass =		USB_TYPE_VENDOR,

	/* Vendor and product id can be overridden by module parameters.  */
	.idVendor =		0x5522,
	.idProduct =		0x1199,
	.bNumConfigurations =	1,
};

static const struct usb_descriptor_header *otg_desc[2];

static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "RFNM Inc",
	[USB_GADGET_PRODUCT_IDX].s = "RFNM",
	[USB_GADGET_SERIAL_IDX].s = "001",
	{  } /* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
	.language       = 0x0409,       /* en-us */
	.strings        = strings_dev,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_dev,
	NULL,
};

static struct usb_function_instance *fi_msg;
static struct usb_function *f_msg;


static struct usb_configuration os_desc_config = {
	.label			= "RFNM Temporary USB Driver",
	.bConfigurationValue	= 1,
	.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
	.next_interface_id = 1,
};	

static int msg_do_config(struct usb_configuration *c)
{
	int ret;

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	c->cdev->use_os_string = 1;
	c->cdev->b_vendor_code = 0xca;

	c->cdev->qw_sign[0] = 'M';
	c->cdev->qw_sign[1] = 0;
	c->cdev->qw_sign[2] = 'S';
	c->cdev->qw_sign[3] = 0;
	c->cdev->qw_sign[4] = 'F';
	c->cdev->qw_sign[5] = 0;
	c->cdev->qw_sign[6] = 'T';
	c->cdev->qw_sign[7] = 0;
	c->cdev->qw_sign[8] = '1';
	c->cdev->qw_sign[9] = 0;
	c->cdev->qw_sign[10] = '0';
	c->cdev->qw_sign[11] = 0;
	c->cdev->qw_sign[12] = '0';
	c->cdev->qw_sign[13] = 0;

	c->cdev->os_desc_config = &os_desc_config;

	printk("Sending os driver data from callback\n");

	f_msg = usb_get_function(fi_msg);
	if (IS_ERR(f_msg))
		return PTR_ERR(f_msg);

	ret = usb_add_function(c, f_msg);
	if (ret)
		goto put_func;

	os_desc_config.interface[0] = f_msg;

	return 0;

put_func:
	usb_put_function(f_msg);
	return ret;
}

static struct usb_configuration msg_config_driver = {
	.label			= "RFNM Temporary USB Driver",
	.bConfigurationValue	= 1,
	.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
};


/****************************** Gadget Bind ******************************/

static int msg_bind(struct usb_composite_dev *cdev)
{
	struct fsg_opts *opts;
	struct fsg_config config;
	int status;

	fi_msg = usb_get_function_instance("RFNM");
	if (IS_ERR(fi_msg))
		return PTR_ERR(fi_msg);

	status = usb_string_ids_tab(cdev, strings_dev);
	if (status < 0)
		goto fail;
	msg_device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;
	msg_device_desc.iSerialNumber =1;

	if (gadget_is_otg(cdev->gadget) && !otg_desc[0]) {
		struct usb_descriptor_header *usb_desc;

		usb_desc = usb_otg_descriptor_alloc(cdev->gadget);
		if (!usb_desc) {
			status = -ENOMEM;
			goto fail;
		}
		usb_otg_descriptor_init(cdev->gadget, usb_desc);
		otg_desc[0] = usb_desc;
		otg_desc[1] = NULL;
	}

	status = usb_add_config(cdev, &msg_config_driver, msg_do_config);
	if (status < 0)
		goto fail;

	

	usb_composite_overwrite_options(cdev, &coverwrite);
	return 0;

fail:
	kfree(otg_desc[0]);
	otg_desc[0] = NULL;
	usb_put_function_instance(fi_msg);
	return status;
}

static int msg_unbind(struct usb_composite_dev *cdev)
{
	if (!IS_ERR(f_msg))
		usb_put_function(f_msg);

	if (!IS_ERR(fi_msg))
		usb_put_function_instance(fi_msg);

	kfree(otg_desc[0]);
	otg_desc[0] = NULL;

	return 0;
}

/****************************** Some noise ******************************/

static struct usb_composite_driver msg_driver = {
	.name		= "rfnm_usb",
	.dev		= &msg_device_desc,
	.max_speed	= USB_SPEED_SUPER_PLUS,
	.needs_serial	= 1,
	.strings	= dev_strings,
	.bind		= msg_bind,
	.unbind		= msg_unbind,
};

module_usb_composite_driver(msg_driver);

MODULE_DESCRIPTION("Mass Storage Gadget");
MODULE_AUTHOR("Michal Nazarewicz");
MODULE_LICENSE("GPL");