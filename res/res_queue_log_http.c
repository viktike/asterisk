/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2025
 *
 * Sperl Viktor <viktike32@gmail.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief HTTP Queue Log Backend
 *
 * \author Sperl Viktor <viktike32@gmail.com.com>
 */

/*** MODULEINFO
	<depend>CURL</depend>
 ***/

#define ASTMM_LIBC ASTMM_IGNORE

#include "asterisk.h"

#include "asterisk/json.h"
#include "asterisk/module.h"
#include "asterisk/config.h"

#include <curl/curl.h>

#define BACKEND_NAME "Queue Custom HTTP Logging"
#define CONFIG_FILE "http_log.conf"
#define CONFIG_CATEGORY "queue"

/* configuration structure with default values */
char *conf_url = "http://127.0.0.1/";
char *conf_date_format = "%Y-%m-%dT%T";
long conf_verify_host = 1;
long conf_verfy_peer = 1;
long conf_timeout = 5;
long conf_connect_timeout = 5;

static struct ast_config * load_config_file(const char * config_file)
{
    struct ast_config *cfg;
    struct ast_flags config_flags = { .flags = 0 };

	cfg = ast_config_load(config_file, config_flags);

	if (!cfg) {
        ast_log(LOG_ERROR, "Error reading config file: %s\n", config_file);
        return NULL;
    } else if (cfg == CONFIG_STATUS_FILEMISSING) {
		ast_log(LOG_WARNING, "Missing configuration file %s\n", config_file);
		return NULL;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Unable to load configuration file %s\n", config_file);
		return NULL;
	} else {
		return cfg;
	}
}

static struct ast_config *config_http(const char *database, const char *table, const char *file, struct ast_config *cfg, struct ast_flags config_flags, const char *unused, const char *who_asked)
{
	ast_log(LOG_NOTICE, "HTTP category for queue_log in: %s", database);
	ast_log(LOG_NOTICE, "HTTP config file for queue_log: %s", table);

	cfg = ast_config_new();
	return cfg;
}

/*
	These are just dummy functions.
	res_queue_log_amqp only implements 'store'.	
 */
static struct ast_variable *realtime_http(const char *database, const char *table, const struct ast_variable *rt_fields)
{
	const struct ast_variable *field = rt_fields;
	struct ast_variable *var=NULL;
	var = ast_variable_new(field->name, field->value, "");
	return var;
}

static struct ast_config *realtime_multi_http(const char *database, const char *table, const struct ast_variable *rt_fields)
{
	const struct ast_variable *field = rt_fields;
	struct ast_variable *var = NULL;
	struct ast_config *cfg = NULL;
	struct ast_category *cat = NULL;

	cfg = ast_config_new();
	cat = ast_category_new_anonymous();
	var = ast_variable_new(field->name, field->value, "");
	ast_variable_append(cat, var);
	ast_category_append(cfg, cat);

	return cfg;
}

static int destroy_http(const char *database, const char *table, const char *keyfield, const char *lookup, const struct ast_variable *rt_fields)
{
	return 0;
}

static int update_http(const char *database, const char *tablename, const char *keyfield, const char *lookup, const struct ast_variable *rt_fields)
{
	return 0;
}

static int update2_http(const char *database, const char *tablename, const struct ast_variable *lookup_fields, const struct ast_variable *update_fields)
{
	return 0;
}

static int require_http(const char *database, const char *tablename, va_list ap)
{
	return 0;
}

static int unload_http(const char *database, const char *tablename)
{
	return 0;
}
/*
	End of dummy functions.
 */

static int store_http(const char *database, const char *table, const struct ast_variable *rt_fields)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	const struct ast_variable *field = rt_fields;
	
	char *conf_url;
	long conf_verify_host, conf_verfy_peer, conf_timeout, conf_connect_timeout;

	CURL *curl;
	CURLcode res;
	struct curl_slist *headers = NULL;

	char *data1 = NULL, *data2 = NULL, *data3 = NULL, *data4 = NULL, *data5 = NULL;
	char *event = NULL, *agent = NULL, *queuename = NULL, *callid = NULL, *time = NULL;

	if(ast_strlen_zero(table)){
		cfg = load_config_file(CONFIG_FILE);
	} else {
		cfg = load_config_file(table);
	}
	if(cfg){
		if(ast_strlen_zero(database)){
			var = ast_variable_browse(cfg, CONFIG_CATEGORY);
		} else {
			var = ast_variable_browse(cfg, database);
		}
		if(var){
			while(var) {
				if (!strcasecmp(var->name, "url")) {
					conf_url = ast_strdupa(var->value);
				} else if (!strcasecmp(var->name, "verify_host")) {
					conf_verify_host = ast_true(var->value) ? 1 : 0;
				} else if (!strcasecmp(var->name, "verify_peer")) {
					conf_verfy_peer = ast_true(var->value) ? 1 : 0;
				} else if (!strcasecmp(var->name, "timeout")) {
					conf_timeout = atol(var->value);
				} else if (!strcasecmp(var->name, "connect_timeout")) {
					conf_connect_timeout = atol(var->value);
				}
				var = var->next;
			}
			ast_config_destroy(cfg);

			while (field) {
				if(!strcmp(field->name, "event")){
					event = ast_strdupa(field->value);
				} else if(!strcmp(field->name, "time")){
					time = ast_strdupa(field->value);
				} else if(!strcmp(field->name, "data1")){
					data1 = ast_strdupa(field->value);
				} else if(!strcmp(field->name, "data2")){
					data2 = ast_strdupa(field->value);
				} else if(!strcmp(field->name, "data3")){
					data3 = ast_strdupa(field->value);
				} else if(!strcmp(field->name, "data4")){
					data4 = ast_strdupa(field->value);
				} else if(!strcmp(field->name, "data5")){
					data5 = ast_strdupa(field->value);
				} else if(!strcmp(field->name, "agent")){
					agent = ast_strdupa(field->value);
				} else if(!strcmp(field->name, "queuename")){
					queuename = ast_strdupa(field->value);
				} else if(!strcmp(field->name, "callid")){
					callid = ast_strdupa(field->value);
				}
				field = field->next;
			}

			curl = curl_easy_init();
			if (curl) {
				headers = curl_slist_append(headers, "Content-Type: application/json");

				curl_easy_setopt(curl, CURLOPT_URL, conf_url);
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

				char *post_fields = malloc(sizeof(char) * 8192);
				sprintf(post_fields, "{\"EventName\": \"%s\", \"EventTime\": \"%s\", \
					\"Data1\": \"%s\", \"Data2\": \"%s\", \"Data3\": \"%s\", \"Data4\": \"%s\", \"Data5\": \"%s\", \
					\"AgentName\": \"%s\", \"QueueName\": \"%s\", \"UniqueID\": \"%s\" }",
					event, time,
					data1, data2, data3, data4, data5,
					agent, queuename, callid
				);

				curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
				curl_easy_setopt(curl, CURLOPT_TIMEOUT, conf_timeout);
				curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, conf_connect_timeout);
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, conf_verify_host);
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, conf_verfy_peer);

				res = curl_easy_perform(curl);
				curl_slist_free_all(headers);
				curl_easy_cleanup(curl);
				free(post_fields);
				if (res != CURLE_OK) {
					ast_log(LOG_WARNING, "HTTP Error: %d\n", res);
					return -1;
				} else {
					return 0;
				}
			} else {
				ast_log(LOG_ERROR, "Could not initialize CURL for %s log\n", CONFIG_CATEGORY);
				return -1;
			}
		} else {
			ast_log(LOG_ERROR, "No config category in config file\n");
			ast_config_destroy(cfg);
			return -1;
		}
	} else {
		return -1;
	}
}

static struct ast_config_engine http_engine = {
	.name = "http",
	.load_func = config_http,
	.realtime_func = realtime_http,
	.realtime_multi_func = realtime_multi_http,
	.store_func = store_http,
	.destroy_func = destroy_http,
	.update_func = update_http,
	.update2_func = update2_http,
	.require_func = require_http,
	.unload_func = unload_http,
};

static int load_module(void)
{
	if (ast_config_engine_register(&http_engine)) {
		ast_log(LOG_NOTICE, BACKEND_NAME " driver loaded.");
		return AST_MODULE_LOAD_SUCCESS;
	} else {
		ast_log(LOG_ERROR, BACKEND_NAME " driver load failed.");
		return AST_MODULE_LOAD_DECLINE;
	}
}

static int unload_module(void)
{
	if (ast_config_engine_deregister(&http_engine)) {
		ast_log(LOG_NOTICE, BACKEND_NAME " driver unloaded.");
		return 0;
	} else {
		ast_log(LOG_ERROR, BACKEND_NAME " driver unload failed.");
		return -1;
	}
}

static int reload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, BACKEND_NAME " backend",
		.load = load_module,
		.unload = unload_module,
		.reload = reload_module,
		.requires = "extconfig"
	);
