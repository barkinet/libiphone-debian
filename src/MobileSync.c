/*
 * MobileSync.c 
 * Contains functions for the built-in MobileSync client.
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

#include <plist/plist.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include "MobileSync.h"
#include "iphone.h"
#include "utils.h"

#define MSYNC_VERSION_INT1 100
#define MSYNC_VERSION_INT2 100

mobilesync_error_t mobilesync_client_new(iphone_device_t device, int dst_port,
						   mobilesync_client_t * client)
{
	if (!device || dst_port == 0 || !client || *client)
		return MOBILESYNC_E_INVALID_ARG;

	mobilesync_error_t ret = MOBILESYNC_E_UNKNOWN_ERROR;

	/* Attempt connection */
	iphone_connection_t connection = NULL;
	if (iphone_device_connect(device, dst_port, &connection) != IPHONE_E_SUCCESS) {
		return ret;
	}

	mobilesync_client_t client_loc = (mobilesync_client_t) malloc(sizeof(struct mobilesync_client_int));
	client_loc->connection = connection;

	/* perform handshake */
	plist_t array = NULL;

	/* first receive version */
	ret = mobilesync_recv(client_loc, &array);

	plist_t msg_node = plist_array_get_item(array, 0);

	char* msg = NULL;
	plist_type type = plist_get_node_type(msg_node);
	if (PLIST_STRING == type) {
		plist_get_string_val(msg_node, &msg);
	}
	if (PLIST_STRING != type || strcmp(msg, "DLMessageVersionExchange") || plist_array_get_size(array) < 3) {
		log_debug_msg("%s: ERROR: MobileSync client expected a version exchange !\n", __func__);
	}
	free(msg);
	msg = NULL;

	plist_t ver_1 = plist_array_get_item(array, 1);
	plist_t ver_2 = plist_array_get_item(array, 2);

	plist_type ver_1_type = plist_get_node_type(ver_1);
	plist_type ver_2_type = plist_get_node_type(ver_2);

	if (PLIST_UINT == ver_1_type && PLIST_UINT == ver_2_type) {

		uint64_t ver_1_val = 0;
		uint64_t ver_2_val = 0;

		plist_get_uint_val(ver_1, &ver_1_val);
		plist_get_uint_val(ver_2, &ver_2_val);

		plist_free(array);
		array = NULL;

		if (ver_1_type == PLIST_UINT && ver_2_type == PLIST_UINT && ver_1_val == MSYNC_VERSION_INT1
			&& ver_2_val == MSYNC_VERSION_INT2) {

			array = plist_new_array();
			plist_array_append_item(array, plist_new_string("DLMessageVersionExchange"));
			plist_array_append_item(array, plist_new_string("DLVersionsOk"));

			ret = mobilesync_send(client_loc, array);

			plist_free(array);
			array = NULL;

			ret = mobilesync_recv(client_loc, &array);
			plist_t rep_node = plist_array_get_item(array, 0);

			type = plist_get_node_type(rep_node);
			if (PLIST_STRING == type) {
				plist_get_string_val(rep_node, &msg);
			}
			if (PLIST_STRING != type || strcmp(msg, "DLMessageDeviceReady")) {
				log_debug_msg("%s: ERROR: MobileSync client failed to start session !\n", __func__);
				ret = MOBILESYNC_E_BAD_VERSION;
			}
			else
			{
				ret = MOBILESYNC_E_SUCCESS;
				*client = client_loc;
			}
			free(msg);
			msg = NULL;

			plist_free(array);
			array = NULL;
		}
	}

	if (MOBILESYNC_E_SUCCESS != ret)
		mobilesync_client_free(client_loc);

	return ret;
}

static void mobilesync_disconnect(mobilesync_client_t client)
{
	if (!client)
		return;

	plist_t array = plist_new_array();
	plist_array_append_item(array, plist_new_string("DLMessageDisconnect"));
	plist_array_append_item(array, plist_new_string("All done, thanks for the memories"));

	mobilesync_send(client, array);
	plist_free(array);
	array = NULL;
}

mobilesync_error_t mobilesync_client_free(mobilesync_client_t client)
{
	if (!client)
		return IPHONE_E_INVALID_ARG;

	mobilesync_disconnect(client);
	return (iphone_device_disconnect(client->connection) == 0 ? MOBILESYNC_E_SUCCESS: MOBILESYNC_E_MUX_ERROR);
}

/** Polls the iPhone for MobileSync data.
 *
 * @param client The MobileSync client
 * @param plist A pointer to the location where the plist should be stored
 *
 * @return an error code
 */
mobilesync_error_t mobilesync_recv(mobilesync_client_t client, plist_t * plist)
{
	if (!client || !plist || (plist && *plist))
		return MOBILESYNC_E_INVALID_ARG;
	mobilesync_error_t ret = MOBILESYNC_E_UNKNOWN_ERROR;
	char *receive = NULL;
	uint32_t datalen = 0, bytes = 0, received_bytes = 0;

	ret = iphone_device_recv(client->connection, (char *) &datalen, sizeof(datalen), &bytes);
	datalen = ntohl(datalen);

	receive = (char *) malloc(sizeof(char) * datalen);

	/* fill buffer and request more packets if needed */
	while ((received_bytes < datalen) && (ret == MOBILESYNC_E_SUCCESS)) {
		ret = iphone_device_recv(client->connection, receive + received_bytes, datalen - received_bytes, &bytes);
		received_bytes += bytes;
	}

	if (ret != MOBILESYNC_E_SUCCESS) {
		free(receive);
		return MOBILESYNC_E_MUX_ERROR;
	}

	plist_from_bin(receive, received_bytes, plist);
	free(receive);

	char *XMLContent = NULL;
	uint32_t length = 0;
	plist_to_xml(*plist, &XMLContent, &length);
	log_dbg_msg(DBGMASK_MOBILESYNC, "%s: plist size: %i\nbuffer :\n%s\n", __func__, length, XMLContent);
	free(XMLContent);

	return ret;
}

/** Sends MobileSync data to the iPhone
 * 
 * @note This function is low-level and should only be used if you need to send
 *        a new type of message.
 *
 * @param client The MobileSync client
 * @param plist The location of the plist to send
 *
 * @return an error code
 */
mobilesync_error_t mobilesync_send(mobilesync_client_t client, plist_t plist)
{
	if (!client || !plist)
		return MOBILESYNC_E_INVALID_ARG;

	char *XMLContent = NULL;
	uint32_t length = 0;
	plist_to_xml(plist, &XMLContent, &length);
	log_dbg_msg(DBGMASK_MOBILESYNC, "%s: plist size: %i\nbuffer :\n%s\n", __func__, length, XMLContent);
	free(XMLContent);

	char *content = NULL;
	length = 0;

	plist_to_bin(plist, &content, &length);

	char *real_query;
	int bytes;
	mobilesync_error_t ret = MOBILESYNC_E_UNKNOWN_ERROR;

	real_query = (char *) malloc(sizeof(char) * (length + 4));
	length = htonl(length);
	memcpy(real_query, &length, sizeof(length));
	memcpy(real_query + 4, content, ntohl(length));

	ret = iphone_device_send(client->connection, real_query, ntohl(length) + sizeof(length), (uint32_t*)&bytes);
	free(real_query);
	return (ret == 0 ? MOBILESYNC_E_SUCCESS: MOBILESYNC_E_MUX_ERROR);
}

