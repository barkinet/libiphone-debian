/**
 * @file libiphone/notification_proxy.h
 * @brief Implementation to talk to the notification proxy on a device
 * \internal
 *
 * Copyright (c) 2009 Nikias Bassen All Rights Reserved.
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

#ifndef NOTIFICATION_PROXY_H
#define NOTIFICATION_PROXY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libiphone/libiphone.h>

/* Error Codes */
#define NP_E_SUCCESS                0
#define NP_E_INVALID_ARG           -1
#define NP_E_PLIST_ERROR           -2

#define NP_E_UNKNOWN_ERROR       -256

typedef int16_t np_error_t;

/* Notification IDs for use with post_notification (client --> device) */
#define NP_SYNC_WILL_START      "com.apple.itunes-mobdev.syncWillStart"
#define NP_SYNC_DID_START       "com.apple.itunes-mobdev.syncDidStart"
#define NP_SYNC_DID_FINISH      "com.apple.itunes-mobdev.syncDidFinish"

/* Notification IDs for use with observe_notification (device --> client) */
#define NP_SYNC_CANCEL_REQUEST  "com.apple.itunes-client.syncCancelRequest"
#define NP_SYNC_SUSPEND_REQUEST "com.apple.itunes-client.syncSuspendRequest"
#define NP_SYNC_RESUME_REQUEST  "com.apple.itunes-client.syncResumeRequest"
#define NP_PHONE_NUMBER_CHANGED "com.apple.mobile.lockdown.phone_number_changed"
#define NP_DEVICE_NAME_CHANGED  "com.apple.mobile.lockdown.device_name_changed"
#define NP_ATTEMPTACTIVATION    "com.apple.springboard.attemptactivation"
#define NP_DS_DOMAIN_CHANGED    "com.apple.mobile.data_sync.domain_changed"
#define NP_APP_INSTALLED        "com.apple.mobile.application_installed"
#define NP_APP_UNINSTALLED      "com.apple.mobile.application_uninstalled"
#define NP_ITDBPREP_DID_END     "com.apple.itdbprep.notification.didEnd"

struct np_client_int;
typedef struct np_client_int *np_client_t;

typedef void (*np_notify_cb_t) (const char *notification);

/* Interface */
np_error_t np_client_new(iphone_device_t device, int dst_port, np_client_t *client);
np_error_t np_client_free(np_client_t client);
np_error_t np_post_notification(np_client_t client, const char *notification);
np_error_t np_observe_notification(np_client_t client, const char *notification);
np_error_t np_observe_notifications(np_client_t client, const char **notification_spec);
np_error_t np_set_notify_callback(np_client_t client, np_notify_cb_t notify_cb);

#ifdef __cplusplus
}
#endif

#endif
