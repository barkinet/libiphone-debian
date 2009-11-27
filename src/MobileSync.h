/* 
 * MobileSync.h
 * Definitions for the built-in MobileSync client
 * 
 * Copyright (c) 2009 Jonathan Beck All Rights Reserved.
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
#ifndef MOBILESYNC_H
#define MOBILESYNC_H

#include "usbmux.h"
#include "iphone.h"
#include "utils.h"

#include <plist/plist.h>



struct iphone_msync_client_int {
	iphone_umux_client_t connection;
};


iphone_error_t iphone_msync_get_all_contacts(iphone_msync_client_t client);

#endif
