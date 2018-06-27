/*
 * USB DVB CI driver - 2.2
 *
 * Copyright (C) 2001-2004 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * This driver is based on the 2.6.3 version of drivers/usb/usb-DVB CI.c
 * but has been rewritten to be easier to read and use.
 *
 */

#include "DVBCIInterface.h"


/* table of devices that work with this driver */
static const struct usb_device_id dvbciusb_table[] = {
	{ USB_INTERFACE_INFO(DVBCI_CLASS, DVBCI_SUBCLASS, DVBCI_PROTOCOL_COMMANDINTERFACE) },
	{ USB_INTERFACE_INFO(DVBCI_CLASS, DVBCI_SUBCLASS, DVBCI_PROTOCOL_MEDIAINTERFACE) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, dvbciusb_table);


void dvbciusb_delete(struct kref *kref)
{
	struct usbdvbci_interface *dev = to_dvbciusb_dev(kref);

	usb_free_urb(dev->bulk_in_urb);
	usb_put_dev(dev->udev);
	kfree(dev->bulk_in_buffer);
	kfree(dev);
}

static int dvbciusb_probe(struct usb_interface *interface,
		      const struct usb_device_id *id)
{
	struct usbdvbci_interface *dev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	size_t buffer_size;
	int i;
	int retval = -ENOMEM;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&interface->dev, "Out of memory\n");
		goto error;
	}
	kref_init(&dev->kref);
	sema_init(&dev->limit_sem, WRITES_IN_FLIGHT);
	mutex_init(&dev->io_read_mutex);
	mutex_init(&dev->io_write_mutex);
	spin_lock_init(&dev->err_lock);
	init_usb_anchor(&dev->submitted);
	init_waitqueue_head(&dev->bulk_in_wait);

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;
	
	/* set up the endpoint information */
	/* use only the first bulk-in and bulk-out endpoints */
	iface_desc = interface->cur_altsetting;
	
	if (iface_desc->desc.bInterfaceProtocol == DVBCI_PROTOCOL_COMMANDINTERFACE)
		dev->interface_type = DVBCIUSB_TYPE_COMMAND;
	else
		dev->interface_type = DVBCIUSB_TYPE_MEDIA;		
		
		
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!dev->bulk_in_endpointAddr &&
		    usb_endpoint_is_bulk_in(endpoint)) {
			/* we found a bulk in endpoint */
			buffer_size = MAX_TRANSFER;
			//usb_endpoint_maxp(endpoint);
			dev->bulk_in_size = buffer_size;
			dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
			dev->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
			if (!dev->bulk_in_buffer) {
				dev_err(&interface->dev,
					"Could not allocate bulk_in_buffer\n");
				goto error;
			}
			dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!dev->bulk_in_urb) {
				dev_err(&interface->dev,
					"Could not allocate bulk_in_urb\n");
				goto error;
			}
		}

		if (!dev->bulk_out_endpointAddr &&
		    usb_endpoint_is_bulk_out(endpoint)) {
			/* we found a bulk out endpoint */
			dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
		}
	}
	if (!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr)) {
		dev_err(&interface->dev,
			"Could not find both bulk-in and bulk-out endpoints\n");
		goto error;
	}

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	/* we can register the device now, as it is ready */
	if (dev->interface_type == DVBCIUSB_TYPE_COMMAND)
		retval = usb_register_dev(interface, &dvbciusb_command_class);
	else
		retval = usb_register_dev(interface, &dvbciusb_media_class);
	
	if (retval) {
		/* something prevented us from registering this driver */
		dev_err(&interface->dev,
			"Not able to get a minor for this device.\n");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	/* let the user know what node this device is now attached to */
	dev_info(&interface->dev,
		 "USB DVB CI device now attached to USB-%d",
		 interface->minor);
	return 0;

error:
	if (dev)
		/* this frees allocated memory */
		kref_put(&dev->kref, dvbciusb_delete);
	return retval;
}

static void dvbciusb_disconnect(struct usb_interface *interface)
{
	struct usbdvbci_interface *dev;
	int minor = interface->minor;
	bool ongoing_io;
	
	dev_info(&interface->dev, "USB DVB CI #%d disconnect request", minor);
	
	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	/* give back our minor */
	usb_deregister_dev(interface, &dvbciusb_command_class);
	
	spin_lock_irq(&dev->err_lock);
	ongoing_io = dev->ongoing_read;
	spin_unlock_irq(&dev->err_lock);

	// kill pending read if on going
	if (ongoing_io)
	{
		dev_info(&interface->dev, "USB DVB CI #%d stop pending I/O", minor);
		usb_unlink_urb(dev->bulk_in_urb);		
	}
	
	/* prevent more I/O from starting */
	mutex_lock(&dev->io_write_mutex);
	mutex_lock(&dev->io_read_mutex);
	dev->interface = NULL;
	mutex_unlock(&dev->io_read_mutex);
	mutex_unlock(&dev->io_write_mutex);

	// kill pending writes
	usb_kill_anchored_urbs(&dev->submitted);

	/* decrement our usage count */
	kref_put(&dev->kref, dvbciusb_delete);

	dev_info(&interface->dev, "USB DVB CI #%d now disconnected", minor);
}

void dvbciusb_draw_down(struct usbdvbci_interface *dev)
{
	int time;

	time = usb_wait_anchor_empty_timeout(&dev->submitted, 1000);
	if (!time)
		usb_kill_anchored_urbs(&dev->submitted);
	usb_kill_urb(dev->bulk_in_urb);
}

static int dvbciusb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usbdvbci_interface *dev = usb_get_intfdata(intf);

	if (!dev)
		return 0;
	dvbciusb_draw_down(dev);
	return 0;
}

static int dvbciusb_resume(struct usb_interface *intf)
{
	return 0;
}

static int dvbciusb_pre_reset(struct usb_interface *intf)
{
	struct usbdvbci_interface *dev = usb_get_intfdata(intf);

	mutex_lock(&dev->io_write_mutex);
	mutex_lock(&dev->io_read_mutex);
	dvbciusb_draw_down(dev);

	return 0;
}

static int dvbciusb_post_reset(struct usb_interface *intf)
{
	struct usbdvbci_interface *dev = usb_get_intfdata(intf);

	/* we are sure no URBs are active - no locking needed */
	dev->errors = -EPIPE;
	mutex_unlock(&dev->io_read_mutex);
	mutex_unlock(&dev->io_write_mutex);

	return 0;
}

struct usb_driver dvbciusb_driver = {
	.name =		"usbdvbci",
	.probe =	dvbciusb_probe,
	.disconnect =	dvbciusb_disconnect,
	.suspend =	dvbciusb_suspend,
	.resume =	dvbciusb_resume,
	.pre_reset =	dvbciusb_pre_reset,
	.post_reset =	dvbciusb_post_reset,
	.id_table =	dvbciusb_table,
	.supports_autosuspend = 1,
};

module_usb_driver(dvbciusb_driver);

MODULE_LICENSE("GPL");