/*
 * NotificationProxy.c
 * Notification Proxy implementation.
 *
 * Copyright (c) 2009 Nikias Bassen, All Rights Reserved.
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

#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <plist/plist.h>
#include "NotificationProxy.h"
#include "utils.h"

struct np_thread {
	iphone_np_client_t client;
	iphone_np_notify_cb_t cbfunc;
};

/** Locks an NP client, done for thread safety stuff.
 *
 * @param client The NP
 */
static void np_lock(iphone_np_client_t client)
{
	log_debug_msg("NP: Locked\n");
	g_mutex_lock(client->mutex);
}

/** Unlocks an NP client, done for thread safety stuff.
 * 
 * @param client The NP
 */
static void np_unlock(iphone_np_client_t client)
{
	log_debug_msg("NP: Unlocked\n");
	g_mutex_unlock(client->mutex);
}

/**
 * Sends an xml plist to the device using the connection specified in client.
 * This function is only used internally.
 *
 * @param client NP to send data to
 * @param dict plist to send
 *
 * @return IPHONE_E_SUCCESS or an error code.
 */
static iphone_error_t np_plist_send(iphone_np_client_t client, plist_t dict)
{
	char *XML_content = NULL;
	uint32_t length = 0;
	uint32_t nlen = 0;
	int bytes = 0;
	iphone_error_t res = IPHONE_E_UNKNOWN_ERROR;

	if (!client || !dict) {
		return IPHONE_E_INVALID_ARG;
	}

	plist_to_xml(dict, &XML_content, &length);

	if (!XML_content || length == 0) {
		return IPHONE_E_PLIST_ERROR;
	}

	nlen = htonl(length);
	iphone_mux_send(client->connection, (const char*)&nlen, sizeof(nlen), (uint32_t*)&bytes);
	if (bytes == sizeof(nlen)) {
		iphone_mux_send(client->connection, XML_content, length, (uint32_t*)&bytes);
		if (bytes > 0) {
			if ((uint32_t)bytes == length) {
				res = IPHONE_E_SUCCESS;
			} else {
				log_debug_msg("%s: ERROR: Could not send all data (%d of %d)!\n", __func__, bytes, length);
			}
		}
	}
	if (bytes <= 0) {
		log_debug_msg("%s: ERROR: sending to device failed.\n", __func__);
	}

	free(XML_content);

	return res;
}

/** Makes a connection to the NP service on the phone. 
 * 
 * @param phone The iPhone to connect on.
 * @param s_port The source port. 
 * @param d_port The destination port. 
 * 
 * @return A handle to the newly-connected client or NULL upon error.
 */
iphone_error_t iphone_np_new_client ( iphone_device_t device, int src_port, int dst_port, iphone_np_client_t *client )
{
	int ret = IPHONE_E_SUCCESS;

	//makes sure thread environment is available
	if (!g_thread_supported())
		g_thread_init(NULL);
	iphone_np_client_t client_loc = (iphone_np_client_t) malloc(sizeof(struct iphone_np_client_int));

	if (!device)
		return IPHONE_E_INVALID_ARG;

	// Attempt connection
	client_loc->connection = NULL;
	ret = iphone_mux_new_client(device, src_port, dst_port, &client_loc->connection);
	if (IPHONE_E_SUCCESS != ret || !client_loc->connection) {
		free(client_loc);
		return ret;
	}

	client_loc->mutex = g_mutex_new();

	client_loc->notifier = NULL;

	*client = client_loc;
	return IPHONE_E_SUCCESS;
}

/** Disconnects an NP client from the phone.
 * 
 * @param client The client to disconnect.
 */
iphone_error_t iphone_np_free_client ( iphone_np_client_t client )
{
	if (!client)
		return IPHONE_E_INVALID_ARG;

	if (client->connection) {
		iphone_mux_free_client(client->connection);
		client->connection = NULL;
		if (client->notifier) {
			log_debug_msg("joining np callback\n");
			g_thread_join(client->notifier);
		}
	}
	if (client->mutex) {
		g_mutex_free(client->mutex);
	}
	free(client);

	return IPHONE_E_SUCCESS;
}

/** Sends a notification to the device's Notification Proxy.
 *
 * notification messages seen so far:
 *   com.apple.itunes-mobdev.syncWillStart
 *   com.apple.itunes-mobdev.syncDidStart
 *
 * @param client The client to send to
 * @param notification The notification message to send
 */
iphone_error_t iphone_np_post_notification( iphone_np_client_t client, const char *notification )
{
	if (!client || !notification) {
		return IPHONE_E_INVALID_ARG;
	}
	np_lock(client);

	plist_t dict = plist_new_dict();
	plist_add_sub_key_el(dict, "Command");
	plist_add_sub_string_el(dict, "PostNotification");
	plist_add_sub_key_el(dict, "Name");
	plist_add_sub_string_el(dict, notification);

	iphone_error_t res = np_plist_send(client, dict);
	plist_free(dict);

	dict = plist_new_dict();
	plist_add_sub_key_el(dict, "Command");
	plist_add_sub_string_el(dict, "Shutdown");

	res = np_plist_send(client, dict);
	plist_free(dict);

	if (res != IPHONE_E_SUCCESS) {
		log_debug_msg("%s: Error sending XML plist to device!\n", __func__);
	}

	np_unlock(client);
	return res;
}

/** Notifies the iphone to send a notification on the specified event.
 *
 * @param client The client to send to
 * @param notification The notifications that should be observed.
 */
iphone_error_t iphone_np_observe_notification( iphone_np_client_t client, const char *notification )
{
	if (!client || !notification) {
		return IPHONE_E_INVALID_ARG;
	}
	np_lock(client);

	plist_t dict = plist_new_dict();
	plist_add_sub_key_el(dict, "Command");
	plist_add_sub_string_el(dict, "ObserveNotification");
	plist_add_sub_key_el(dict, "Name");
	plist_add_sub_string_el(dict, notification);

	iphone_error_t res = np_plist_send(client, dict);
	if (res != IPHONE_E_SUCCESS) {
		log_debug_msg("%s: Error sending XML plist to device!\n", __func__);
	}
	plist_free(dict);

	np_unlock(client);
	return res;
}


/** Notifies the iphone to send a notification on specified events.
 *
 * observation messages seen so far:
 *   com.apple.itunes-client.syncCancelRequest
 *   com.apple.itunes-client.syncSuspendRequest
 *   com.apple.itunes-client.syncResumeRequest
 *   com.apple.mobile.lockdown.phone_number_changed
 *   com.apple.mobile.lockdown.device_name_changed
 *   com.apple.springboard.attemptactivation
 *   com.apple.mobile.data_sync.domain_changed
 *   com.apple.mobile.application_installed
 *   com.apple.mobile.application_uninstalled
 *
 * @param client The client to send to
 * @param notification_spec Specification of the notifications that should be
 *  observed. This is expected to be an array of const char* that MUST have a
 *  terminating NULL entry. However this parameter can be NULL; in this case,
 *  the default set of notifications will be used.
 */
iphone_error_t iphone_np_observe_notifications( iphone_np_client_t client, const char **notification_spec )
{
	int i = 0;
	iphone_error_t res = IPHONE_E_UNKNOWN_ERROR;
	const char **notifications = notification_spec;

	if (!client) {
		return IPHONE_E_INVALID_ARG;
	}

	if (!notifications) {
		notifications = np_default_notifications;
	}

	while (notifications[i]) {
		res = iphone_np_observe_notification(client, notifications[i]);
		if (res != IPHONE_E_SUCCESS) {
			break;
		}
		i++;
	}

	return res;
}

/**
 * Checks if a notification has been sent.
 *
 * @param client NP to get a notification from
 * @param notification Pointer to a buffer that will be allocated and filled
 *  with the notification that has been received.
 *
 * @return IPHONE_E_SUCCESS if a notification has been received,
 *         IPHONE_E_TIMEOUT if nothing has been received,
 *         or an error value if an error occured.
 *
 * @note You probably want to check out iphone_np_set_notify_callback
 * @see iphone_np_set_notify_callback
 */
iphone_error_t iphone_np_get_notification( iphone_np_client_t client, char **notification )
{
	uint32_t bytes = 0;
	iphone_error_t res;
	uint32_t pktlen = 0;
	char *XML_content = NULL;
	plist_t dict = NULL;

	if (!client || !client->connection || *notification) {
		return IPHONE_E_INVALID_ARG;
	}

	np_lock(client);

	iphone_mux_recv_timeout(client->connection, (char*)&pktlen, sizeof(pktlen), &bytes, 500);
	log_debug_msg("NotificationProxy: initial read=%i\n", bytes);
	if (bytes < 4) {
		log_debug_msg("NotificationProxy: no notification received!\n");
		res = IPHONE_E_TIMEOUT;
	} else {
		if ((char)pktlen == 0) {
			pktlen = ntohl(pktlen);
			log_debug_msg("NotificationProxy: %d bytes following\n", pktlen);
			XML_content = (char*)malloc(pktlen);
			log_debug_msg("pointer %p\n", XML_content);

			iphone_mux_recv_timeout(client->connection, XML_content, pktlen, &bytes, 1000);
			if (bytes <= 0) {
				res = IPHONE_E_UNKNOWN_ERROR;
			} else {
				log_debug_msg("NotificationProxy: received data:\n");
				log_debug_buffer(XML_content, pktlen);

				plist_from_xml(XML_content, bytes, &dict);
				if (!dict) {
					np_unlock(client);
					return IPHONE_E_PLIST_ERROR;
				}

				plist_t cmd_key_node = plist_find_node_by_key(dict, "Command");
				plist_t cmd_value_node = plist_get_next_sibling(cmd_key_node);
				char *cmd_value = NULL;

				if (plist_get_node_type(cmd_value_node) == PLIST_STRING) {
					plist_get_string_val(cmd_value_node, &cmd_value);
				}

				if (cmd_value && !strcmp(cmd_value, "RelayNotification")) {
					plist_t name_key_node = plist_get_next_sibling(cmd_value_node);
					plist_t name_value_node = plist_get_next_sibling(name_key_node);

					char *name_key = NULL;
					char *name_value = NULL;

					if (plist_get_node_type(name_key_node) == PLIST_KEY) {
						plist_get_key_val(name_key_node, &name_key);
					}
					if (plist_get_node_type(name_value_node) == PLIST_STRING) {
						plist_get_string_val(name_value_node, &name_value);
					}

					res = IPHONE_E_PLIST_ERROR;
					if (name_key && name_value && !strcmp(name_key, "Name")) {
						*notification = name_value;
						log_debug_msg("%s: got notification %s\n", __func__, name_value);
						res = IPHONE_E_SUCCESS;
					}
					free(name_key);
				} else if (cmd_value && !strcmp(cmd_value, "ProxyDeath")) {
					log_debug_msg("%s: ERROR: NotificationProxy died!\n", __func__);
					res = IPHONE_E_UNKNOWN_ERROR;
				} else if (cmd_value) {
					log_debug_msg("%d: unknown NotificationProxy command '%s' received!\n", __func__);
					res = IPHONE_E_UNKNOWN_ERROR;
				} else {
					res = IPHONE_E_PLIST_ERROR;
				}
				if (cmd_value) {
					free(cmd_value);
				}
				plist_free(dict);
				dict = NULL;
				free(XML_content);
				XML_content = NULL;
			}
		} else {
			res = IPHONE_E_UNKNOWN_ERROR;
		}
	}

	np_unlock(client);

	return res;
}

/**
 * Internally used thread function.
 */
gpointer iphone_np_notifier( gpointer arg )
{
	char *notification = NULL;
	struct np_thread *npt = (struct np_thread*)arg;

	if (!npt) return NULL;

	log_debug_msg("%s: starting callback.\n", __func__);
	while (npt->client->connection) {
		iphone_np_get_notification(npt->client, &notification);
		if (notification) {
			npt->cbfunc(notification);
			free(notification);
			notification = NULL;
		}
		sleep(1);
	}
	if (npt) {
		free(npt);
	}

	return NULL;
}

/**
 * This function allows an application to define a callback function that will
 * be called when a notification has been received.
 * It will start a thread that polls for notifications and calls the callback
 * function if a notification has been received.
 *
 * @param client the NP client
 * @param notify_cb pointer to a callback function or NULL to de-register a
 *        previously set callback function
 *
 * @return IPHONE_E_SUCCESS when the callback was successfully registered,
 *         or an error value when an error occured.
 */
iphone_error_t iphone_np_set_notify_callback( iphone_np_client_t client, iphone_np_notify_cb_t notify_cb )
{
	if (!client) {
		return IPHONE_E_INVALID_ARG;
	}
	iphone_error_t res = IPHONE_E_UNKNOWN_ERROR;

	np_lock(client);
	if (client->notifier) {
		log_debug_msg("%s: callback already set, removing\n");
		iphone_umux_client_t conn = client->connection;
		client->connection = NULL;
		g_thread_join(client->notifier);
		client->notifier = NULL;
		client->connection = conn;
	}

	if (notify_cb) {
		struct np_thread *npt = (struct np_thread*)malloc(sizeof(struct np_thread));
		if (npt) {
			npt->client = client;
			npt->cbfunc = notify_cb;

			client->notifier = g_thread_create(iphone_np_notifier, npt, TRUE, NULL);
			if (client->notifier) {
				res = IPHONE_E_SUCCESS;
			}
		}
	} else {
		log_debug_msg("%s: no callback set\n", __func__);
	}
	np_unlock(client);

	return res;
}
