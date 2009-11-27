/* 
 * iphone.c
 * Functions for creating and initializing iPhone structures.
 *
 * Copyright (c) 2008 Zach C. All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA 
 */

#include "usbmux.h"
#include "iphone.h"
#include "utils.h"
#include <arpa/inet.h>
#include <usb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * This function sets the configuration of the given device to 3
 * and claims the interface 1. If usb_set_configuration fails, it detaches
 * the kernel driver that blocks the device, and retries configuration.
 *
 * @param phone which device to configure
 */
static void iphone_config_usb_device(iphone_device_t phone)
{
	int ret;
	int bytes;
	unsigned char buf[512];

	log_debug_msg("setting configuration... ");
	ret = usb_set_configuration(phone->device, 3);
	if (ret != 0) {
		log_debug_msg("Hm, usb_set_configuration returned %d: %s, trying to fix:\n", ret, strerror(-ret));
		log_debug_msg("-> detaching kernel driver... ");
		ret =
			usb_detach_kernel_driver_np(phone->device,
										phone->__device->config->interface->altsetting->bInterfaceNumber);
		if (ret != 0) {
			log_debug_msg("usb_detach_kernel_driver_np returned %d: %s\n", ret, strerror(-ret));
		} else {
			log_debug_msg("done.\n");
			log_debug_msg("setting configuration again... ");
			ret = usb_set_configuration(phone->device, 3);
			if (ret != 0) {
				log_debug_msg("Error: usb_set_configuration returned %d: %s\n", ret, strerror(-ret));
			} else {
				log_debug_msg("done.\n");
			}
		}
	} else {
		log_debug_msg("done.\n");
	}

	log_debug_msg("claiming interface... ");
	ret = usb_claim_interface(phone->device, 1);
	if (ret != 0) {
		log_debug_msg("Error: usb_claim_interface returned %d: %s\n", ret, strerror(-ret));
	} else {
		log_debug_msg("done.\n");
	}

	do {
		bytes = usb_bulk_read(phone->device, BULKIN, (void *) &buf, 512, 800);
		if (bytes > 0) {
			log_debug_msg("iphone_config_usb_device: initial read returned %d bytes of data.\n", bytes);
			log_debug_buffer(buf, bytes);
		}
	} while (bytes > 0);
}

/**
 * Given a USB bus and device number, returns a device handle to the iPhone on
 * that bus. To aid compatibility with future devices, this function does not
 * check the vendor and device IDs! To do that, you should use
 * iphone_get_device() or a system-specific API (e.g. HAL).
 *
 * @param bus_n The USB bus number.
 * @param dev_n The USB device number.
 * @param device A pointer to a iphone_device_t, which must be set to NULL upon
 *      calling iphone_get_specific_device, which will be filled with a device
 *      descriptor on return. 
 * @return IPHONE_E_SUCCESS if ok, otherwise an error code.
 */
iphone_error_t iphone_get_specific_device(unsigned int bus_n, int dev_n, iphone_device_t * device)
{
	struct usb_bus *bus, *busses;
	struct usb_device *dev;
	usbmux_version_header *version;
	int bytes = 0;

	//check we can actually write in device
	if (!device || (device && *device))
		return IPHONE_E_INVALID_ARG;

	iphone_device_t phone = (iphone_device_t) malloc(sizeof(struct iphone_device_int));

	// Initialize the struct
	phone->device = NULL;
	phone->__device = NULL;
	phone->buffer = NULL;

	// Initialize libusb
	usb_init();
	usb_find_busses();
	usb_find_devices();
	busses = usb_get_busses();

	// Set the device configuration
	for (bus = busses; bus; bus = bus->next)
		if (strtoul(bus->dirname, NULL, 10) == bus_n)
			for (dev = bus->devices; dev != NULL; dev = dev->next)
				if (strtol(dev->filename, NULL, 10) == dev_n) {
					phone->__device = dev;
					phone->device = usb_open(phone->__device);
					iphone_config_usb_device(phone);
					goto found;
				}

	iphone_free_device(phone);

	log_debug_msg("iphone_get_specific_device: iPhone not found\n");
	return IPHONE_E_NO_DEVICE;

  found:
	// Send the version command to the phone
	version = version_header();
	bytes = usb_bulk_write(phone->device, BULKOUT, (char *) version, sizeof(*version), 800);
	if (bytes < 20) {
		log_debug_msg("get_iPhone(): libusb did NOT send enough!\n");
		if (bytes < 0) {
			log_debug_msg("get_iPhone(): libusb gave me the error %d: %s (%s)\n",
						  bytes, usb_strerror(), strerror(-bytes));
		}
	}
	// Read the phone's response
	bytes = usb_bulk_read(phone->device, BULKIN, (char *) version, sizeof(*version), 800);

	// Check for bad response
	if (bytes < 20) {
		free(version);
		iphone_free_device(phone);
		log_debug_msg("get_iPhone(): Invalid version message -- header too short.\n");
		if (bytes < 0)
			log_debug_msg("get_iPhone(): libusb error message %d: %s (%s)\n", bytes, usb_strerror(), strerror(-bytes));
		return IPHONE_E_NOT_ENOUGH_DATA;
	}
	// Check for correct version
	if (ntohl(version->major) == 1 && ntohl(version->minor) == 0) {
		// We're all ready to roll.
		log_debug_msg("get_iPhone() success\n");
		free(version);
		*device = phone;
		return IPHONE_E_SUCCESS;
	} else {
		// Bad header
		log_debug_msg("get_iPhone(): Received a bad header/invalid version number.\n");
		log_debug_buffer((char *) version, sizeof(*version));
		iphone_free_device(phone);
		free(version);
		return IPHONE_E_BAD_HEADER;
	}

	// If it got to this point it's gotta be bad
	log_debug_msg("get_iPhone(): Unknown error.\n");
	iphone_free_device(phone);
	free(version);
	return IPHONE_E_UNKNOWN_ERROR;	// if it got to this point it's gotta be bad
}

/**
 * Scans all USB busses and devices for a known AFC-compatible device and
 * returns a handle to the first such device it finds. Known devices include
 * those with vendor ID 0x05ac and product ID between 0x1290 and 0x1293
 * inclusive.
 *
 * This function is convenient, but on systems where higher-level abstractions
 * (such as HAL) are available it may be preferable to use
 * iphone_get_specific_device instead, because it can deal with multiple
 * connected devices as well as devices not known to libiphone.
 * 
 * @param device Upon calling this function, a pointer to a location of type
 *  iphone_device_t, which must have the value NULL. On return, this location
 *  will be filled with a handle to the device.
 * @return IPHONE_E_SUCCESS if ok, otherwise an error code.
 */
iphone_error_t iphone_get_device(iphone_device_t * device)
{
	struct usb_bus *bus;
	struct usb_device *dev;

	usb_init();
	usb_find_busses();
	usb_find_devices();

	for (bus = usb_get_busses(); bus != NULL; bus = bus->next)
		for (dev = bus->devices; dev != NULL; dev = dev->next)
			if (dev->descriptor.idVendor == 0x05ac
				&& dev->descriptor.idProduct >= 0x1290 && dev->descriptor.idProduct <= 0x1293)
				return iphone_get_specific_device(strtoul(bus->dirname, NULL, 10), strtol(dev->filename, NULL, 10), device);

	return IPHONE_E_NO_DEVICE;
}

/** Cleans up an iPhone structure, then frees the structure itself.  
 * This is a library-level function; deals directly with the iPhone to tear
 *  down relations, but otherwise is mostly internal.
 * 
 * @param phone A pointer to an iPhone structure.
 */
iphone_error_t iphone_free_device(iphone_device_t device)
{
	if (!device)
		return IPHONE_E_INVALID_ARG;
	iphone_error_t ret = IPHONE_E_UNKNOWN_ERROR;
	int bytes;
	unsigned char buf[512];

	// read final package(s)
	if (device->device != NULL) {
		do {
			bytes = usb_bulk_read(device->device, BULKIN, (void *) &buf, 512, 800);
			if (bytes > 0) {
				log_debug_msg("iphone_free_device: final read returned\n");
				log_debug_buffer(buf, bytes);
			}
		} while (bytes > 0);
	}

	if (device->buffer) {
		free(device->buffer);
	}
	if (device->device) {
		usb_release_interface(device->device, 1);
		usb_close(device->device);
		ret = IPHONE_E_SUCCESS;
	}
	free(device);
	return ret;
}

/** Sends data to the phone
 * This is a low-level (i.e. directly to phone) function.
 * 
 * @param phone The iPhone to send data to
 * @param data The data to send to the iPhone
 * @param datalen The length of the data
 * @return The number of bytes sent, or -1 on error or something.
 */
int send_to_phone(iphone_device_t phone, char *data, int datalen)
{
	if (!phone)
		return -1;
	int bytes = 0;

	if (!phone)
		return -1;
	log_debug_msg("send_to_phone: Attempting to send datalen = %i data = %p\n", datalen, data);

	bytes = usb_bulk_write(phone->device, BULKOUT, data, datalen, 800);
	if (bytes < datalen) {
		if (bytes < 0)
			log_debug_msg("send_to_iphone(): libusb gave me the error %d: %s - %s\n", bytes, usb_strerror(),
						  strerror(-bytes));
		return -1;
	} else {
		return bytes;
	}
	/* Should not be reached */
	return -1;
}

/** This function is a low-level (i.e. direct to iPhone) function.
 * 
 * @param phone The iPhone to receive data from
 * @param data Where to put data read
 * @param datalen How much data to read in
 * @param timeout How many milliseconds to wait for data
 * 
 * @return How many bytes were read in, or -1 on error.
 */
int recv_from_phone(iphone_device_t phone, char *data, int datalen, int timeout)
{
	if (!phone)
		return -1;
	int bytes = 0;

	if (!phone)
		return -1;
	log_debug_msg("recv_from_phone(): attempting to receive %i bytes\n", datalen);

	bytes = usb_bulk_read(phone->device, BULKIN, data, datalen, timeout);
	if (bytes < 0) {
		log_debug_msg("recv_from_phone(): libusb gave me the error %d: %s (%s)\n", bytes, usb_strerror(),
					  strerror(-bytes));
		return -1;
	}

	return bytes;
}
