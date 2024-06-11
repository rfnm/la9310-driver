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
#include <linux/io.h>
#include <linux/rfnm-shared.h>

#include "drivers/usb/gadget/function/f_mass_storage.h"

/*-------------------------------------------------------------------------*/
USB_GADGET_COMPOSITE_OPTIONS();

static struct usb_device_descriptor rfnm_device_desc = {
	.bLength =		sizeof rfnm_device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	/* .bcdUSB = DYNAMIC */
#if 0
	.bDeviceClass =		0xEF, //0,//USB_TYPE_VENDOR,
	.bDeviceSubClass = 0x02,
	.bDeviceProtocol = 0x01,
#else
	.bDeviceClass =		0,//USB_TYPE_VENDOR,
#endif

	/* Vendor and product id can be overridden by module parameters.  */
	.idVendor =		0x15A2,
	.idProduct =		0x8D,
	.bNumConfigurations =	1,
};

static const struct usb_descriptor_header *otg_desc[2];

char rfnm_serial[9] = "00000000\0";

static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "RFNM Inc",
	[USB_GADGET_PRODUCT_IDX].s = "RFNM Data Boost",
	[USB_GADGET_SERIAL_IDX].s = rfnm_serial,
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

static struct usb_function_instance *fi_rfnm;


static struct usb_function *f_rfnm;


static struct usb_configuration os_desc_config = {
	.label			= "RFNM Temporary USB Driver",
	.bConfigurationValue	= 1,
	.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
	.next_interface_id = 1,
};	

static int rfnm_do_config(struct usb_configuration *c)
{
	int ret;
#if 0
	if (gadget_is_otg(c->cdev->gadget)) {

		printk("gadget is OTG\n");

		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}
#endif
#if 1
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
#endif
	printk("Sending os driver data from callback\n");

	f_rfnm = usb_get_function(fi_rfnm);
	if (IS_ERR(f_rfnm))
		return PTR_ERR(f_rfnm);

	ret = usb_add_function(c, f_rfnm);
	if (ret)
		goto put_func;

	os_desc_config.interface[0] = f_rfnm;


	printk("callback ok\n");


	return 0;

put_func:
	usb_put_function(f_rfnm);
	return ret;
}



static struct usb_configuration rfnm_config_driver = {
	.label			= "RFNM Temporary USB Driver",
	.bConfigurationValue	= 1,
	.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
};

char backing_storage[] = "/home/root/backing_storage";

static struct fsg_module_parameters msg_mod_data = {
	.stall = 1,
	//.file[0] = backing_storage
};
#define fsg_num_buffers	CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS

FSG_MODULE_PARAMETERS(/* no prefix */, msg_mod_data);

static struct usb_configuration msg_config_driver = {
	.label			= "Linux File-Backed Storage",
	.bConfigurationValue	= 1,
	.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
};

/****************************** Gadget Bind ******************************/

extern void rfnm_wsled_send_chain(uint8_t chain_id);
extern void rfnm_wsled_set(uint8_t chain_id, uint8_t led_id, uint8_t r, uint8_t g, uint8_t b);

static int rfnm_bind(struct usb_composite_dev *cdev)
{
	struct fsg_opts *msg_opts;
	struct fsg_config msg_config;
	int status;

	int r, g, b;
	r = 0xff;
	g = 0xc0;
	b = 0x00;

	rfnm_wsled_set(0, 0, 0, 0, 0xff);
	rfnm_wsled_send_chain(0);

	fi_rfnm = usb_get_function_instance("RFNM");
	if (IS_ERR(fi_rfnm))
		return PTR_ERR(fi_rfnm);

	status = usb_string_ids_tab(cdev, strings_dev);
	if (status < 0)
		goto fail;

	struct rfnm_bootconfig *cfg;
	cfg = memremap(RFNM_BOOTCONFIG_PHYADDR, SZ_4M, MEMREMAP_WB);
	memcpy(rfnm_serial, cfg->motherboard_eeprom.serial_number, 8);
	memunmap(cfg);

	rfnm_device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;
	rfnm_device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
	rfnm_device_desc.iSerialNumber = strings_dev[USB_GADGET_SERIAL_IDX].id;
	
	status = usb_add_config(cdev, &rfnm_config_driver, rfnm_do_config);
	if (status < 0)
		goto fail;

	usb_composite_overwrite_options(cdev, &coverwrite);
	return 0;

fail:
	kfree(otg_desc[0]);
	otg_desc[0] = NULL;
	usb_put_function_instance(fi_rfnm);
	return status;
}

static int rfnm_unbind(struct usb_composite_dev *cdev)
{
	if (!IS_ERR(f_rfnm))
		usb_put_function(f_rfnm);

	if (!IS_ERR(fi_rfnm))
		usb_put_function_instance(fi_rfnm);

	kfree(otg_desc[0]);
	otg_desc[0] = NULL;

	return 0;
}

/****************************** Some noise ******************************/

static struct usb_composite_driver rfnm_driver = {
	.name		= "rfnm_usb_boost",
	.udc_name = "38200000.usb",
	.dev		= &rfnm_device_desc,
	.max_speed	= USB_SPEED_SUPER_PLUS,
	.needs_serial	= 1,
	.strings	= dev_strings,
	.bind		= rfnm_bind,
	.unbind		= rfnm_unbind,
};

module_usb_composite_driver(rfnm_driver);

//module_driver(__usb_composite_driver, usb_composite_probe, usb_composite_unregister);


MODULE_DESCRIPTION("Mass Storage Gadget");
MODULE_AUTHOR("Michal Nazarewicz");
MODULE_LICENSE("GPL");
