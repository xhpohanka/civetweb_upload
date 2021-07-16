/*
 * Copyright (c) 2019 Antmicro Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <posix/pthread.h>
#include <data/json.h>
#include <drivers/watchdog.h>

#define LOG_LEVEL LOG_LEVEL_DBG

#include <logging/log.h>
LOG_MODULE_REGISTER(cvtest);


#include "civetweb.h"

#define HTTP_PORT	80
#define HTTPS_PORT	443

#define CIVETWEB_MAIN_THREAD_STACK_SIZE		CONFIG_MAIN_STACK_SIZE

/* Use samllest possible value of 1024 (see the line 18619 of civetweb.c) */
#define MAX_REQUEST_SIZE_BYTES			1024

K_THREAD_STACK_DEFINE(civetweb_stack, CIVETWEB_MAIN_THREAD_STACK_SIZE);

struct civetweb_info {
	const char *version;
	const char *os;
	uint32_t features;
	const char *feature_list;
	const char *build;
	const char *compiler;
	const char *data_model;
};

#define FIELD(struct_, member_, type_) { \
	.field_name = #member_, \
	.field_name_len = sizeof(#member_) - 1, \
	.offset = offsetof(struct_, member_), \
	.type = type_ \
}

void send_ok(struct mg_connection *conn)
{
	mg_printf(conn,
		  "HTTP/1.1 200 OK\r\n"
		  "Content-Type: text/html\r\n"
		  "Connection: close\r\n\r\n");
}


static int field_get_fw(const char *key, const char *value, size_t valuelen, void *user_data)
{
	LOG_DBG("file chunk size: %d", valuelen);

	return MG_FORM_FIELD_HANDLE_GET;
}

static int field_found(const char *key,
	                   const char *filename,
	                   char *path,
	                   size_t pathlen,
	                   void *user_data)
{
	return MG_FORM_FIELD_STORAGE_GET;
}

int fwupdate_handler(struct mg_connection *conn, void *cbdata)
{
	struct mg_form_data_handler fdh = {field_found, field_get_fw, NULL, NULL};

	LOG_DBG("update start");


	int ret = mg_handle_form_request(conn, &fdh);
	LOG_DBG("form ret %d", ret);

	mg_printf(conn,"HTTP/1.1 200 OK\r\n"
			"Content-Type: application/json\r\n"
			"Connection: close\r\n"
			"\r\n");
	mg_printf(conn, "{\"err\":%d}", 0);

	return 200;
}

int hello_world_handler(struct mg_connection *conn, void *cbdata)
{
	send_ok(conn);
	mg_printf(conn, "<html><body>");
	mg_printf(conn, "<h3>Hello World from Zephyr!</h3>");
	mg_printf(conn, "See also:\n");
	mg_printf(conn, "<ul>\n");
	mg_printf(conn, "<li><a href=/info>system info</a></li>\n");
	mg_printf(conn, "<li><a href=/history>cookie demo</a></li>\n");
	mg_printf(conn, "</ul>\n");
	mg_printf(conn, "</body></html>\n");

	return 200;
}

int system_info_handler(struct mg_connection *conn, void *cbdata)
{
	static const struct json_obj_descr descr[] = {
		FIELD(struct civetweb_info, version, JSON_TOK_STRING),
		FIELD(struct civetweb_info, os, JSON_TOK_STRING),
		FIELD(struct civetweb_info, feature_list, JSON_TOK_STRING),
		FIELD(struct civetweb_info, build, JSON_TOK_STRING),
		FIELD(struct civetweb_info, compiler, JSON_TOK_STRING),
		FIELD(struct civetweb_info, data_model, JSON_TOK_STRING),
	};

	struct civetweb_info info = {};
	char info_str[1024] = {};
	int ret;
	int size;

	size = mg_get_system_info(info_str, sizeof(info_str));

	ret = json_obj_parse(info_str, size, descr, ARRAY_SIZE(descr), &info);

	send_ok(conn);

	if (ret < 0) {
		mg_printf(conn, "Could not retrieve: %d\n", ret);
		return 500;
	}


	mg_printf(conn, "<html><body>");

	mg_printf(conn, "<h3>Server info</h3>");
	mg_printf(conn, "<ul>\n");
	mg_printf(conn, "<li>host os - %s</li>\n", info.os);
	mg_printf(conn, "<li>server - civetweb %s</li>\n", info.version);
	mg_printf(conn, "<li>compiler - %s</li>\n", info.compiler);
	mg_printf(conn, "<li>board - %s</li>\n", "");
	mg_printf(conn, "</ul>\n");

	mg_printf(conn, "</body></html>\n");

	return 200;
}

int history_handler(struct mg_connection *conn, void *cbdata)
{
	const struct mg_request_info *req_info = mg_get_request_info(conn);
	const char *cookie = mg_get_header(conn, "Cookie");
	char history_str[64];

	mg_get_cookie(cookie, "history", history_str, sizeof(history_str));

	mg_printf(conn, "HTTP/1.1 200 OK\r\n");
	mg_printf(conn, "Connection: close\r\n");
	mg_printf(conn, "Set-Cookie: history='%s'\r\n", req_info->local_uri);
	mg_printf(conn, "Content-Type: text/html\r\n\r\n");

	mg_printf(conn, "<html><body>");

	mg_printf(conn, "<h3>Your URI is: %s<h3>\n", req_info->local_uri);

	if (history_str[0] == 0) {
		mg_printf(conn, "<h5>This is your first visit.</h5>\n");
	} else {
		mg_printf(conn, "<h5>your last /history visit was: %s</h5>\n",
			  history_str);
	}

	mg_printf(conn, "Some cookie-saving links to try:\n");
	mg_printf(conn, "<ul>\n");
	mg_printf(conn, "<li><a href=/history/first>first</a></li>\n");
	mg_printf(conn, "<li><a href=/history/second>second</a></li>\n");
	mg_printf(conn, "<li><a href=/history/third>third</a></li>\n");
	mg_printf(conn, "<li><a href=/history/fourth>fourth</a></li>\n");
	mg_printf(conn, "<li><a href=/history/fifth>fifth</a></li>\n");
	mg_printf(conn, "</ul>\n");

	mg_printf(conn, "</body></html>\n");

	return 200;
}

void *main_pthread(void *arg)
{
	static const char * const options[] = {
		"listening_ports",
		STRINGIFY(HTTP_PORT),
		"num_threads",
		"1",
		"max_request_size",
		STRINGIFY(MAX_REQUEST_SIZE_BYTES),
		NULL
	};

	struct mg_callbacks callbacks;
	struct mg_context *ctx;

	(void)arg;

	memset(&callbacks, 0, sizeof(callbacks));
	ctx = mg_start(&callbacks, 0, (const char **)options);

	if (ctx == NULL) {
		printf("Unable to start the server.");
		return 0;
	}

	mg_set_request_handler(ctx, "/$", hello_world_handler, 0);
	mg_set_request_handler(ctx, "/info$", system_info_handler, 0);
	mg_set_request_handler(ctx, "/history", history_handler, 0);

	mg_set_request_handler(ctx, "/fwupdate$", fwupdate_handler, 0);

	return 0;
}

void main(void)
{
	pthread_attr_t civetweb_attr;
	pthread_t civetweb_thread;

	int wdt_channel_id = 0;
	const struct device *wdt = device_get_binding("IWDG_1");
	if (wdt) {
		struct wdt_timeout_cfg wdt_config;

		/* Reset SoC when watchdog timer expires. */
		wdt_config.flags = WDT_FLAG_RESET_SOC;

		wdt_config.window.min = 0U;
		wdt_config.window.max = 8000U;
		wdt_config.callback = NULL;

		wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
		if (wdt_channel_id < 0)
			LOG_ERR("Watchdog install error");

		if (wdt_setup(wdt, 0) < 0)
			LOG_ERR("Watchdog setup error");

		wdt_feed(wdt, wdt_channel_id);

	}

	(void)pthread_attr_init(&civetweb_attr);
	(void)pthread_attr_setstack(&civetweb_attr, &civetweb_stack,
				    CIVETWEB_MAIN_THREAD_STACK_SIZE);

	(void)pthread_create(&civetweb_thread, &civetweb_attr,
			     &main_pthread, 0);

	while (1) {
		k_sleep(K_MSEC(1000));
		if (wdt)
			wdt_feed(wdt, wdt_channel_id);
	}

}
