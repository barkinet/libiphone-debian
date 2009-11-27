/*
 * iphoneinfo.c
 * Simple utility to show information about an attached device
 *
 * Copyright (c) 2009 Martin Szulecki All Rights Reserved.
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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <libiphone/libiphone.h>
#include <libiphone/lockdown.h>

#define FORMAT_KEY_VALUE 1
#define FORMAT_XML 2

static const char *domains[] = {
	"com.apple.disk_usage",
	"com.apple.mobile.battery",
/* FIXME: For some reason lockdownd segfaults on this, works sometimes though 
	"com.apple.mobile.debug",. */
	"com.apple.xcode.developerdomain",
	"com.apple.international",
	"com.apple.mobile.mobile_application_usage",
	"com.apple.mobile.backup",
	"com.apple.mobile.user_preferences",
	"com.apple.mobile.sync_data_class",
	"com.apple.mobile.software_behavior",
	"com.apple.mobile.iTunes.SQLMusicLibraryPostProcessCommands",
	"com.apple.iTunes",
	"com.apple.mobile.iTunes.store",
	"com.apple.mobile.iTunes",
	NULL
};

int is_domain_known(char *domain);
void print_usage(int argc, char **argv);
void plist_node_to_string(plist_t node);
void plist_children_to_string(plist_t node);

int main(int argc, char *argv[])
{
	lockdownd_client_t client = NULL;
	iphone_device_t phone = NULL;
	iphone_error_t ret = IPHONE_E_UNKNOWN_ERROR;
	int i;
	int format = FORMAT_KEY_VALUE;
	char uuid[41];
	char *domain = NULL;
	char *key = NULL;
	char *xml_doc = NULL;
	uint32_t xml_length;
	plist_t node = NULL;
	uuid[0] = 0;

	/* parse cmdline args */
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--debug")) {
			iphone_set_debug_mask(DBGMASK_ALL);
			iphone_set_debug_level(1);
			continue;
		}
		else if (!strcmp(argv[i], "-u") || !strcmp(argv[i], "--uuid")) {
			i++;
			if (!argv[i] || (strlen(argv[i]) != 40)) {
				print_usage(argc, argv);
				return 0;
			}
			strcpy(uuid, argv[i]);
			continue;
		}
		else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--domain")) {
			i++;
			if (!argv[i] || (strlen(argv[i]) < 4)) {
				print_usage(argc, argv);
				return 0;
			}
			if (!is_domain_known(argv[i])) {
				fprintf(stderr, "WARNING: Sending query with unknown domain \"%s\".\n", argv[i]);
			}
			domain = strdup(argv[i]);
			continue;
		}
		else if (!strcmp(argv[i], "-k") || !strcmp(argv[i], "--key")) {
			i++;
			if (!argv[i] || (strlen(argv[i]) <= 1)) {
				print_usage(argc, argv);
				return 0;
			}
			key = strdup(argv[i]);
			continue;
		}
		else if (!strcmp(argv[i], "-x") || !strcmp(argv[i], "--xml")) {
			format = FORMAT_XML;
			continue;
		}
		else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			print_usage(argc, argv);
			return 0;
		}
		else {
			print_usage(argc, argv);
			return 0;
		}
	}

	if (uuid[0] != 0) {
		ret = iphone_device_new(&phone, uuid);
		if (ret != IPHONE_E_SUCCESS) {
			printf("No device found with uuid %s, is it plugged in?\n", uuid);
			return -1;
		}
	}
	else
	{
		ret = iphone_device_new(&phone, NULL);
		if (ret != IPHONE_E_SUCCESS) {
			printf("No device found, is it plugged in?\n");
			return -1;
		}
	}

	if (LOCKDOWN_E_SUCCESS != lockdownd_client_new(phone, &client)) {
		iphone_device_free(phone);
		return -1;
	}

	/* run query and output information */
	if(lockdownd_get_value(client, domain, key, &node) == LOCKDOWN_E_SUCCESS)
	{
		if (plist_get_node_type(node) == PLIST_DICT) {
			if (plist_dict_get_size(node))
			{
				switch (format) {
				case FORMAT_XML:
					plist_to_xml(node, &xml_doc, &xml_length);
					printf("%s", xml_doc);
					free(xml_doc);
					break;
				case FORMAT_KEY_VALUE:
				default:
					plist_children_to_string(node);
					break;
				}
			}
		}
		else if(node && (key != NULL))
			plist_node_to_string(node);
		if (node)
			plist_free(node);
		node = NULL;
	}

	if (domain != NULL)
		free(domain);
	lockdownd_client_free(client);
	iphone_device_free(phone);

	return 0;
}

int is_domain_known(char *domain)
{
	int i = 0;
	while (domains[i] != NULL) {
		if (strstr(domain, domains[i++])) {
			return 1;
		}
	}
	return 0;
}

void print_usage(int argc, char **argv)
{
	int i = 0;
	char *name = NULL;
	
	name = strrchr(argv[0], '/');
	printf("Usage: %s [OPTIONS]\n", (name ? name + 1: argv[0]));
	printf("Show information about the first connected iPhone/iPod Touch.\n\n");
	printf("  -d, --debug\t\tenable communication debugging\n");
	printf("  -u, --uuid UUID\ttarget specific device by its 40-digit device UUID\n");
	printf("  -q, --domain NAME\tset domain of query to NAME. Default: None\n");
	printf("  -k, --key NAME\tonly query key specified by NAME. Default: All keys.\n");
	printf("  -x, --xml\t\toutput information as xml plist instead of key/value pairs\n");
	printf("  -h, --help\t\tprints usage information\n");
	printf("\n");
	printf("  Known domains are:\n\n");
	while (domains[i] != NULL) {
		printf("  %s\n", domains[i++]);
	}
	printf("\n");
}

void plist_node_to_string(plist_t node)
{
	char *s = NULL;
	double d;
	uint8_t b;

	uint64_t u = 0;

	plist_type t;

	if (!node)
		return;

	t = plist_get_node_type(node);

	switch (t) {
	case PLIST_BOOLEAN:
		plist_get_bool_val(node, &b);
		printf("%s\n", (b ? "true" : "false"));
		break;

	case PLIST_UINT:
		plist_get_uint_val(node, &u);
		printf("%llu\n", (long long)u);
		break;

	case PLIST_REAL:
		plist_get_real_val(node, &d);
		printf("%f\n", d);
		break;

	case PLIST_STRING:
		plist_get_string_val(node, &s);
		printf("%s\n", s);
		free(s);
		break;

	case PLIST_KEY:
		plist_get_key_val(node, &s);
		printf("%s: ", s);
		free(s);
		break;

	case PLIST_DATA:
		printf("\n");
		break;
	case PLIST_DATE:
		printf("\n");
		break;
	case PLIST_ARRAY:
	case PLIST_DICT:
		printf("\n");
		plist_children_to_string(node);
		break;
	default:
		break;
	}
}

void plist_children_to_string(plist_t node)
{
	/* iterate over key/value pairs */
	plist_dict_iter it = NULL;

	char* key = NULL;
	plist_t subnode = NULL;
	plist_dict_new_iter(node, &it);
	plist_dict_next_item(node, it, &key, &subnode);
	while (subnode)
	{
		subnode = NULL;

		printf("%s: ", key);
		free(key);
		key = NULL;
		plist_node_to_string(subnode);

		plist_dict_next_item(node, it, &key, &subnode);
	}
	free(it);
}

