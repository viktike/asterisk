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
 * \brief AMQP Publisher Dialplan Appication
 *
 * \author Sperl Viktor <sperl.viktor@arenim.com>
 */

/*** MODULEINFO
	<depend>res_amqp</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/amqp.h"
#include "asterisk/config.h"
#include "asterisk/app.h"
#include "asterisk/pbx.h"

/*** DOCUMENTATION
	<application name="PublishAMQP" language="en_US">
		<synopsis>
			Publish a message to a Rabbit Message Queue
		</synopsis>
		<syntax>
			<parameter name="connection" required="true"/>
			<parameter name="queue" required="true"/>
			<parameter name="message" required="true"/>
			<parameter name="exchange" required="false"/>
			<parameter name="options" required="false">
				<optionlist>
					<option name="c">
						<argument name="content-type" required="true" />
						<para>Set the content type (mime type string)</para>
					</option>
					<option name="d">
						<argument name="delivery-mode" required="true" />
						<para>Set the delivery mode (integer)</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Sets the following channel variable:</para>
			<variablelist>
				<variable name="AMQPSTATUS">
					<para>The result of the publish operation.</para>
					<value name="SUCCESS" />
					<value name="MISSING" />
					<value name="INVALID" />
					<value name="FAILURE" />
				</variable>
			</variablelist>
		</description>
	</application>
 ***/

static char *app = "PublishAMQP";

enum option_flags {
	OPTION_DELIVERY_MODE    = (1 << 0),
	OPTION_CONTENT_TYPE     = (1 << 1),
};

enum option_args {
	OPTION_ARG_DELIVERY_MODE,
	OPTION_ARG_CONTENT_TYPE,
	/* This *must* be the last value in this enum! */
	OPTION_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(app_opts, {
	AST_APP_OPTION_ARG('d', OPTION_DELIVERY_MODE, OPTION_ARG_DELIVERY_MODE),
	AST_APP_OPTION_ARG('c', OPTION_CONTENT_TYPE, OPTION_ARG_CONTENT_TYPE),
});

static int publish_exec(struct ast_channel *chan, const char *data)
{
	char *parse, *opts[OPTION_ARG_ARRAY_SIZE];
	struct ast_flags flags;

	char *connection = NULL, *queue = NULL, *message, *exchange;
	
	int delivery_mode;
	char *content_type;

	int res = 0;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(connection);
		AST_APP_ARG(queue);
		AST_APP_ARG(message);
		AST_APP_ARG(exchange);
		AST_APP_ARG(options);
	);

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.connection)) {
		ast_log(LOG_ERROR, "%s requires an AMQP connection from res_amqp", app);
		res = 1;
	} else {
		connection = ast_strdupa(args.connection);
	}

	if (ast_strlen_zero(args.queue)) {
		ast_log(LOG_ERROR, "%s requires an AMQP queue to publish to", app);
		res = 1;
	} else {
		queue = ast_strdupa(args.queue);
	}

	if (ast_strlen_zero(args.message)) {
		ast_log(LOG_ERROR, "%s requires a message to be published to AMQP", app);
		res = 1;
	} else {
		message = ast_strdupa(args.message);
	}

	if (ast_strlen_zero(args.exchange)) {
		exchange = ast_strdupa("");
	} else {
		exchange = ast_strdupa(args.exchange);
	}

	content_type = ast_strdupa("text/plain");
	delivery_mode = 2;
	if (args.argc == 5) {

		ast_app_parse_options(app_opts, &flags, opts, args.options);

		if (ast_test_flag(&flags, OPTION_DELIVERY_MODE) && !ast_strlen_zero(opts[OPTION_ARG_DELIVERY_MODE])) {
			delivery_mode = atoi(opts[OPTION_ARG_DELIVERY_MODE]);
		}
		if (ast_test_flag(&flags, OPTION_CONTENT_TYPE) && !ast_strlen_zero(opts[OPTION_ARG_CONTENT_TYPE])) {
			content_type = ast_strdupa(opts[OPTION_ARG_CONTENT_TYPE]);
		}
	}

	if(res){
		pbx_builtin_setvar_helper(chan, "AMQPSTATUS", "MISSING");
		return -1;
	} else {

		amqp_basic_properties_t props = {
			._flags = AMQP_BASIC_DELIVERY_MODE_FLAG | AMQP_BASIC_CONTENT_TYPE_FLAG,
			.delivery_mode = delivery_mode, /* persistent delivery mode */
			.content_type = amqp_cstring_bytes(content_type)
		};

		struct ast_amqp_connection *cxn = ast_amqp_get_connection(connection);

		if(!cxn){
			ast_log(LOG_ERROR, "Invalid connection: %s", connection);
			pbx_builtin_setvar_helper(chan, "AMQPSTATUS", "INVALID");
			return -1;
		} else {
			res = ast_amqp_basic_publish(cxn,
				amqp_cstring_bytes(exchange),
				amqp_cstring_bytes(queue),
				0, /* mandatory; don't return unsendable messages */
				0, /* immediate; allow messages to be queued */
				&props,
				amqp_cstring_bytes(message)
			);

			if (res) {
				ast_log(LOG_ERROR, "Error publishing %s to AMQP: %d", queue, res);
				pbx_builtin_setvar_helper(chan, "AMQPSTATUS", "FAILURE");
				return -1;
			} else {
				pbx_builtin_setvar_helper(chan, "AMQPSTATUS", "SUCCESS");
				return res;
			}
		}
	}
}

static int load_module(void)
{
	if (ast_register_application_xml(app, publish_exec)) {
		return AST_MODULE_LOAD_SUCCESS;
	} else {
		return AST_MODULE_LOAD_DECLINE;
	}
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int reload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "AMQP Publisher Dialplan Application",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.requires = "res_amqp",
);
