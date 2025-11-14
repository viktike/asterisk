/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, House of the Hat, where the baby cries 
 * and the mother dont see.
 *
 * Amim Knabben - amim.knabben@gmail.com
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
#include "asterisk/cel.h"
#include "asterisk/json.h"

#include <curl/curl.h>

#define BACKEND_NAME "CEL Custom HTTP Logging"
#define CONFIG_FILE "http_log.conf"
#define CONFIG_CATEGORY "cel"

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

static void http_log(struct ast_event *event)
{
	CURL *curl;
	CURLcode res;
	struct ast_tm timeresult;
	struct curl_slist *headers = NULL;
	char start_time[80] = "";

	struct ast_cel_event_record record = {
		.version = AST_CEL_EVENT_RECORD_VERSION,
	};

	if (ast_cel_fill_record(event, &record)) {
		return;
	}

	ast_localtime(&record.event_time, &timeresult, NULL);
	ast_strftime(start_time, sizeof(start_time), conf_date_format, &timeresult);

	curl = curl_easy_init();
	if (curl) {
		headers = curl_slist_append(headers, "Content-Type: application/json");

		curl_easy_setopt(curl, CURLOPT_URL, conf_url);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		char *post_fields = malloc(sizeof(char) * 8192);
		sprintf(post_fields, "{\"EventName\": \"%s\", \"SubEvent\": \"%s\", \
			\"AccountCode\": \"%s\", \
			\"CallerIDnum\": \"%s\", \"CallerIDname\": \"%s\", \
			\"CallerIDani\": \"%s\", \"CallerIDrdnis\": \"%s\",	\
			\"CAllerIDdnid\": \"%s\", \"Exten\": \"%s\", \
			\"Context\": \"%s\", \"Channel\": \"%s\", \
			\"Application\": \"%s\", \"AppData\": \"%s\", \
			\"EventTime\": \"%s\", \"AMAFlags\": \"%s\", \
			\"UniqueID\": \"%s\", \"LinkedID\": \"%s\", \
			\"Userfield\": \"%s\", \"Peer\": \"%s\", \
			\"Peeraccount\": \"%s\", \"Extra\": %s }",
			record.event_name, record.user_defined_name, record.account_code, record.caller_id_num,
			record.caller_id_name, record.caller_id_ani, record.caller_id_rdnis,
			record.caller_id_dnid, record.extension, record.context,
			record.channel_name, record.application_name, record.application_data,
			start_time, ast_channel_amaflags2string(record.amaflag),
			record.unique_id, record.linked_id, record.user_field, record.peer,
			record.peer_account, record.extra);

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
		} 
	} else {
		ast_log(LOG_ERROR, "Could not initialize CURL for %s log\n", CONFIG_CATEGORY);
	}
}

static int load_module(void)
{
	conf_url = "http://127.0.0.1/" CONFIG_CATEGORY "/";
	if (load_config(CONFIG_FILE, CONFIG_CATEGORY)) {
		return AST_MODULE_LOAD_FAILURE;
	}

	if (ast_cel_backend_register(BACKEND_NAME, http_log)) {
		return AST_MODULE_LOAD_FAILURE;
	}

	ast_log(LOG_NOTICE, "Loaded " BACKEND_NAME " Module\n");
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "CEL (Channel Event Logging) with CURL over HTTP (HyperText Transport Protocol)",
	.load = load_module,
	.unload = unload_module,
	.reload = reload_config,
	.requires = "cel"
);
