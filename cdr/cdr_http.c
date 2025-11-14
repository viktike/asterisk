/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, House of the Hat, where the baby cries 
 * and the mother dont see.
 *
 * Sperl Viktor <viktike32@gmail.com>
 */

/*** MODULEINFO
	<depend>CURL</depend>
 ***/

#define ASTMM_LIBC ASTMM_IGNORE

#include "asterisk.h"

#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/json.h"

#include <curl/curl.h>

#define BACKEND_NAME "CDR Custom HTTP Logging"
#define CONFIG_FILE "http_log.conf"
#define CONFIG_CATEGORY "cdr"

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
    struct ast_flags config_flags = {0};

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

static int load_config(const char *config_file, const char *config_category)
{
	struct ast_config *cfg = load_config_file(config_file);
	struct ast_variable *var;

	if(!cfg){
		return -1;
	} else {
		var = ast_variable_browse(cfg, config_category);
		if (!var) {
			ast_log(LOG_ERROR, "No config category %s in file %s\n", config_category, config_file);
			ast_config_destroy(cfg);
			return -1;
		} else {
			while(var) {
				if (!strcasecmp(var->name, "url")) {
					conf_url = ast_strdupa(var->value);
				} else if (!strcasecmp(var->name, "date_format")) {
					conf_date_format = ast_strdupa(var->value);
				} else if (!strcasecmp(var->name, "verify_host")) {
					conf_verify_host = ast_true(var->value) ? 1 : 0;
				} else if (!strcasecmp(var->name, "verify_peer")) {
					conf_verfy_peer = ast_true(var->value) ? 1 : 0;
				} else if (!strcasecmp(var->name, "timeout")) {
					conf_timeout = atol(var->value);
				} else if (!strcasecmp(var->name, "connect_timeout")) {
					conf_connect_timeout = atol(var->value);
				} else {
					ast_log(LOG_WARNING, "Unknown config variable %s in category %s in file %s\n", var->name, config_category, config_file);
				}
				var = var->next;
			}
		}
		ast_config_destroy(cfg);
		ast_log(LOG_NOTICE, "Configuration category %s loaded from %s\n", config_category, config_file);
		return 0;
	}
}

static int reload_config(void)
{
	ast_log(LOG_NOTICE, "Reloading " BACKEND_NAME " configuration\n");
	return load_config(CONFIG_FILE, CONFIG_CATEGORY);	
}

static int unload_module(void)
{
	ast_log(LOG_NOTICE, "Unloading " BACKEND_NAME " module\n");
	return 0;
}

static int http_log(struct ast_cdr *event)
{
	CURL *curl;
	CURLcode res;
	struct ast_tm starttimeresult, answertimeresult, endtimeresult;
	struct curl_slist *headers = NULL;
	char start_time[80] = "", answer_time[80] = "", end_time[80] = "";

	ast_localtime(&event->start, &starttimeresult, NULL);
	ast_strftime(start_time, sizeof(start_time), conf_date_format, &starttimeresult);

	ast_localtime(&event->answer, &answertimeresult, NULL);
	ast_strftime(answer_time, sizeof(answer_time), conf_date_format, &answertimeresult);

	ast_localtime(&event->end, &endtimeresult, NULL);
	ast_strftime(end_time, sizeof(end_time), conf_date_format, &endtimeresult);

	curl = curl_easy_init();
	if (curl) {
		headers = curl_slist_append(headers, "Content-Type: application/json");

		curl_easy_setopt(curl, CURLOPT_URL, conf_url);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		char *post_fields = malloc(sizeof(char) * 8192);
		sprintf(post_fields, "{\"src\": \"%s\", \"dst\": \"%s\", \
			\"clid\": \"%s\", \"dcontext\": \"%s\", \
			\"channel\": \"%s\", \"dstchannel\": \"%s\", \
			\"lastapp\": \"%s\", \"lastdata\": \"%s\", \
			\"disposition\": \"%s\", \"amaflags\": \"%s\", \
			\"accountcode\": \"%s\", \"userfield\": \"%s\", \
			\"uniqueid\": \"%s\", \"linkedid\": \"%s\", \
			\"duration\": %ld, \"billsec\": %ld, \
			\"peeraccount\": \"%s\", \"sequence\": %d, \
			\"start\": \"%s\", \"answer\": \"%s\", \"end\": \"%s\" }",
			event->src, event->dst, event->clid, event->dcontext,
			event->channel, event->dstchannel,
			event->lastapp, event->lastdata,
			ast_cdr_disp2str(event->disposition), ast_channel_amaflags2string(event->amaflags),
			event->accountcode, event->userfield,
			event->uniqueid, event->linkedid,
			event->duration, event->billsec,
			event->peeraccount, event->sequence,
			start_time, answer_time, end_time
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
}

static int load_module(void)
{
	conf_url = "http://127.0.0.1/" CONFIG_CATEGORY "/";
	if (load_config(CONFIG_FILE, CONFIG_CATEGORY)) {
		return AST_MODULE_LOAD_FAILURE;
	}

	if (ast_cdr_register(BACKEND_NAME, "CDR (Call Detail Record) with CURL over HTTP (HyperText Transport Protocol)", http_log)) {
		return AST_MODULE_LOAD_FAILURE;
	}

	ast_log(LOG_NOTICE, "Loaded " BACKEND_NAME " Module\n");
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "CDR (Call Detail Record) with CURL over HTTP (HyperText Transport Protocol)",
	.load = load_module,
	.unload = unload_module,
	.reload = reload_config,
	.requires = "cdr"
);
