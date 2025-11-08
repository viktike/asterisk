/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2024, Arenim Technologies, Ltd.
 *
 * Sperl Viktor <sperl.viktor@arenim.com>
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
 * \brief AMQP Queue Log Backend
 *
 * \author Sperl Viktor <sperl.viktor@arenim.com>
 */

/*** MODULEINFO
	<depend>res_amqp</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/json.h"
#include "asterisk/module.h"
#include "asterisk/amqp.h"
#include "asterisk/config.h"

static struct ast_config *config_amqp(const char *database, const char *table, const char *file, struct ast_config *cfg, struct ast_flags config_flags, const char *unused, const char *who_asked)
{
	ast_log(LOG_NOTICE, "AMQP connection for queue_log: %s", database);
	ast_log(LOG_NOTICE, "AMQP queue for queue_log: %s", table);

	cfg = ast_config_new();
	return cfg;
}

/*
	These are just dummy functions.
	res_queue_log_amqp only implements 'store'.	
 */
static struct ast_variable *realtime_amqp(const char *database, const char *table, const struct ast_variable *rt_fields)
{
	const struct ast_variable *field = rt_fields;
	struct ast_variable *var=NULL;
	var = ast_variable_new(field->name, field->value, "");
	return var;
}

static struct ast_config *realtime_multi_amqp(const char *database, const char *table, const struct ast_variable *rt_fields)
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

static int destroy_amqp(const char *database, const char *table, const char *keyfield, const char *lookup, const struct ast_variable *rt_fields)
{
	return 0;
}

static int update_amqp(const char *database, const char *tablename, const char *keyfield, const char *lookup, const struct ast_variable *rt_fields)
{
	return 0;
}

static int update2_amqp(const char *database, const char *tablename, const struct ast_variable *lookup_fields, const struct ast_variable *update_fields)
{
	return 0;
}

static int require_amqp(const char *database, const char *tablename, va_list ap)
{
	return 0;
}

static int unload_amqp(const char *database, const char *tablename)
{
	return 0;
}
/*
	End of dummy functions.
 */

static int store_amqp(const char *database, const char *table, const struct ast_variable *rt_fields)
{
	const struct ast_variable *field = rt_fields;

	char *data1 = NULL, *data2 = NULL, *data3 = NULL, *data4 = NULL, *data5 = NULL;
	char *event = NULL, *agent = NULL, *queuename = NULL, *callid = NULL, *time = NULL;

	int res = 0;

	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	RAII_VAR(char *, str, NULL, ast_json_free);

	amqp_basic_properties_t props = {
		._flags = AMQP_BASIC_DELIVERY_MODE_FLAG | AMQP_BASIC_CONTENT_TYPE_FLAG,
		.delivery_mode = 2, /* persistent delivery mode */
		.content_type = amqp_cstring_bytes("application/json")
	};

	while (field) {
		if(!strcmp(field->name, "event")){
			event = ast_strdupa(field->value);
			res = 1;
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
		} else {
			ast_log(LOG_WARNING, "Unknown queue_log field: %s with value: %s", field->name, field->value);
		}
		field = field->next;
	}

	if(!res){
		ast_log(LOG_ERROR, "The 'event' attribute not found in queue_log entry, not publishing it to AMQP");
		return -1;
	}

	if(!time){
		json = ast_json_pack("{"
			/* event, agent, queuename, callerid */
			"s: s, s: s, s: s, s: s, "
			/* data1, data2, data3, data4, data5 */
			"s: s, s: s, s: s, s: s, s: s, "
			/* time */
			"s: o "
			"}",

			"event", event,
			"agent", agent,
			"queuename", queuename,
			"callid", callid,

			"data1", data1,
			"data2", data2,
			"data3", data3,
			"data4", data4,
			"data5", data5,

			"time", ast_json_timeval(ast_tvnow(), NULL)
		);
	} else {
		json = ast_json_pack("{"
			/* event, agent, queuename, callerid */
			"s: s, s: s, s: s, s: s, "
			/* data1, data2, data3, data4, data5 */
			"s: s, s: s, s: s, s: s, s: s, "
			/* time */
			"s: s "
			"}",

			"event", event,
			"agent", agent,
			"queuename", queuename,
			"callid", callid,

			"data1", data1,
			"data2", data2,
			"data3", data3,
			"data4", data4,
			"data5", data5,

			"time", time
		);
	}

	if (!json) {
		ast_log(LOG_ERROR, "Failed to build JSON from queue_log");
		return -1;
	}

	/* Dump the JSON to a string for publication */
	str = ast_json_dump_string(json);
	if (!str) {
		ast_log(LOG_ERROR, "Failed to build string from queue_log JSON");
		return -1;
	}

	struct ast_amqp_connection *cxn = ast_amqp_get_connection(database);

	if(!cxn){
		ast_log(LOG_ERROR, "Invalid connection: %s", database);
		return -1;
	} else {

		res = ast_amqp_basic_publish(cxn,
			amqp_cstring_bytes(""),
			amqp_cstring_bytes(table),
			0, /* mandatory; don't return unsendable messages */
			0, /* immediate; allow messages to be queued */
			&props,
			amqp_cstring_bytes(str));

		if (res) {
			ast_log(LOG_ERROR, "Error publishing queue_log to AMQP: %d", res);
			return -1;
		} else {
			return 1;
		}
	}
}

static struct ast_config_engine amqp_engine = {
	.name = "amqp",
	.load_func = config_amqp,
	.realtime_func = realtime_amqp,
	.realtime_multi_func = realtime_multi_amqp,
	.store_func = store_amqp,
	.destroy_func = destroy_amqp,
	.update_func = update_amqp,
	.update2_func = update2_amqp,
	.require_func = require_amqp,
	.unload_func = unload_amqp,
};

static int load_module(void)
{
	if (ast_config_engine_register(&amqp_engine)) {
		ast_log(LOG_NOTICE, "AMQP Queue Log driver loaded.");
		return AST_MODULE_LOAD_SUCCESS;
	} else {
		ast_log(LOG_ERROR, "AMQP Queue Log driver load failed.");
		return AST_MODULE_LOAD_DECLINE;
	}
}

static int unload_module(void)
{
	if (ast_config_engine_deregister(&amqp_engine)) {
		ast_log(LOG_NOTICE, "AMQP Queue Log driver unloaded.");
		return 0;
	} else {
		ast_log(LOG_ERROR, "AMQP Queue Log driver unload failed.");
		return -1;
	}
}

static int reload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "AMQP Queue Log Backend",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.reload = reload_module,
		.load_pri = AST_MODPRI_CDR_DRIVER,
		.requires = "extconfig,res_amqp",
	);
