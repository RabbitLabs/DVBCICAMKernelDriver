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

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>


#define  DVBCI_CLASS                           0xEF	//!< USB Miscellaneous class
#define  DVBCI_SUBCLASS                        0x07	//!< USB Miscellaneous DVB-CI subclass
#define  DVBCI_PROTOCOL_COMMANDINTERFACE       0x01	//!< USB DVB CI Command Interface Protocol
#define  DVBCI_PROTOCOL_MEDIAINTERFACE         0x02	//!< USB DVB CI Media Interface Protocol

/* our private defines. if this grows any larger, use your own .h file */
#define MAX_TRANSFER		(PAGE_SIZE - 512)
/* MAX_TRANSFER is chosen so that the VM is not stressed by
   allocations > PAGE_SIZE and the number of packets in a page
   is an integer 512 is the largest possible packet on EHCI */
#define WRITES_IN_FLIGHT	8
/* arbitrarily chosen */


typedef enum { DVBCIUSB_TYPE_COMMAND, DVBCIUSB_TYPE_MEDIA } usbdcvbci_interface_type_e;


/* Structure to hold all of our device specific stuff */
struct usbdvbci_interface {
  struct usb_device	*udev;			/* the usb device for this device */
  struct usb_interface	*interface;		/* the interface for this device */
  usbdcvbci_interface_type_e interface_type;
  struct semaphore	limit_sem;		/* limiting the number of writes in progress */
  struct usb_anchor	submitted;		/* in case we need to retract our submissions */
  struct urb		*bulk_in_urb;		/* the urb to read data with */
  unsigned char           *bulk_in_buffer;	/* the buffer to receive data */
  size_t			bulk_in_size;		/* the size of the receive buffer */
  size_t			bulk_in_filled;		/* number of bytes in the buffer */
  size_t			bulk_in_copied;		/* already copied to user space */
  size_t      bulk_in_requested; /* number of bytes requested for read */
  bool bulk_in_short_packet;  /* last packet received was a short packet */
  __u8			bulk_in_endpointAddr;	/* the address of the bulk in endpoint */
  __u8			bulk_out_endpointAddr;	/* the address of the bulk out endpoint */
  int			errors;			/* the last request tanked */
  bool			ongoing_read;		/* a read is going on */
  spinlock_t		err_lock;		/* lock for errors */
  struct kref		kref;
  struct mutex		io_read_mutex;		/* synchronize I/O with disconnect */
  struct mutex		io_write_mutex;		/* synchronize I/O with disconnect */
  wait_queue_head_t	bulk_in_wait;		/* to wait for an ongoing read */
};

void dvbciusb_delete(struct kref *kref);
void dvbciusb_draw_down(struct usbdvbci_interface *dev);

#define to_dvbciusb_dev(d) container_of(d, struct usbdvbci_interface, kref)

/* Get a minor range for your devices from the usb maintainer */
#define USB_DVBCIUSB_COMMAND_MINOR_BASE	192
#define USB_DVBCIUSB_MEDIA_MINOR_BASE	208

extern struct usb_class_driver dvbciusb_command_class;
extern struct usb_class_driver dvbciusb_media_class;
