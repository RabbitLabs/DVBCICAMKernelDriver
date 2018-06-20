# DVBCICAMKernelDriver
A Linux Kernel driver for DVB CI CAM using CI+ 2.0 specifications

# Description

This driver is handling both command interface (Class 0xEF, Subclass 0x07, Protocol 0x01) and media interface (Class 0xEF, Subclass 0x07, Protocol 0x02) from DVB CI+ USB physical layer (see DVB Bluebook A173-1 https://www.dvb.org/resources/public/standards/a173-1_dvb-ci_plus2_-_usb_implementation.pdf )

This driver is shamelessly based on the USB Skeleton example.

# How to use

Open device for command interface usbdvbcicmd%d. Read to device returns full SPDU (if the caller provide large enough buffer). If the driver returns a full buffer, check SPDU header for the exact amount of data to read and issue subsequent reads for the rest of SPDU. Write shall always send a complete SPDU.

Device for media usbdvbcimedia%d is working the same way. Read will stop when receiving a short or null packet. Write will always be terminated by short or null packet.

# Limitations

- numbering of interface device is not consistant: first CAM is seen as usbdvbcicmd0 and usbdvbcimedia1. Need a better handling of IAD to provide consistant device numbering
- separation of cmd and media interface could be questionned
- Not heavily tested for now

# Disclaimer

This piece of work is licensed under GPL V2 license and is provided for free with no liabilities (i.e. use at your own risk)

This is work is NOT endorsed by DVB
