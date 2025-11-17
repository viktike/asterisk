
/*! \file
 *
 * \brief NetworkBlocking() - Block a call with 603+ handling
 *
 *
 * \ingroup applications
 *
 * \author Darrin M. Gorski <darrin@gorski.net>
 *
 */

#include "asterisk.h"
#include "asterisk/pbx.h"
#include "asterisk/strings.h"
#include "asterisk/astobj2.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/datastore.h"
#include "asterisk/app.h"
#include "asterisk/config.h"
#include "asterisk/paths.h"
#include "asterisk/causes.h"

#include <pjsip_ua.h>
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"

/* https://access.atis.org/higherlogic/ws/public/download/67424/ATIS-1000099.pdf */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_session</depend>
	<depend>chan_pjsip</depend>
	<support_level>extended</support_level>
 ***/

/*** DOCUMENTATION
	<application name="NetworkBlocking" language="en_US">
		<synopsis>
			Block (hangup) an unanswered call with "603+" handling.
		</synopsis>
		<syntax>
			<parameter name="options">
				<optionlist>

					<option name="s">
						<para>Use SIP protocol values in Reason header (default)</para>
					</option>

					<option name="q">
						<para>Use Q.850 protocol values in Reason header</para>
					</option>
	
					<option name="l">
						<argument name="location" required="true">
							<para>The location of element that is blocking this call, using location specifiers from RFC 8606.</para>
							<para>Will default to "RPN" which means "blocking occurred in the private network serving the called party"</para>
							<para>This field is not checked for valid values.</para>
						</argument>
					</option>

					<option name="u">
						<argument name="url" required="true">
							<para>A URL to provide in the Reason header for a caller that is being blocked to contact for remediation</para>
							<para>Per 603+: The "url" parameter value shall be a valid HTTPS URL resolvable via the public DNS.</para>
						</argument>
					</option>

					<option name="t">
						<argument name="tel" required="true">
							<para>An E.164 telephone number to provide in the Reason header for a caller that is being blocked to contact for remediation</para>
							<para>Per 603+: The "tel" parameter value shall be a valid telephone number in global E.164 format.</para>
						</argument>
					</option>

					<option name="e">
						<argument name="email" required="true">
							<para>An email address to provide in the Reason header for a caller that is being blocked to contact for remediation</para>
							<para>Per 603+: The "email" parameter value shall be a valid email address.</para>
						</argument>
					</option>

					<option name="i">
						<argument name="id" required="true">
							<para>An ID value to provide in the Reason header for a caller to use to facilitate redress</para>
							<para>Per 603+: Identifier used by the service provider that blocked the call to facilitate redress (e.g., call identifier, blocking reason identifier, network segment identifier, etc.). The "id" parameter value shall be a string containing only alpha, digit, underscore, and/or dash characters and shall have a length of no more than 64 characters.</para>
							<para>The sepecification states that an ID can be specified but is not required.</para>
						</argument>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application implements "603+" Network Blocking semantics as defined in ATIS-1000099.  It behaves similar to caling Hangup(21) - that is, it rejects a call with a 603 response code.  However, the reason is changed from "Declined" to "Network Blocking" and a "603+" compliant Reason header is added that contains the required elements based on the specification.  If options processing fails or one of the required options is not provided, it issues a warning and performs a regular Hangup(21). This results in a call rejected with a plain 603 response without the additional "603+" processing.</para>
			<para>At least one of "u", "t", or "e" options are required in order to build a compliant Reason header.</para>
			<para>Option arguments are not verified as legal values (e.g. the t option doesn't verify it's argument is a valid E.164 number)</para>
			<para>See the reference below for a link to the ATIS-1000099 Robocall/Newtork Blocking specification.</para>
		</description>
		<see-also>
			<ref type="link">https://access.atis.org/higherlogic/ws/public/download/67424/ATIS-1000099.pdf</ref>
			<ref type="application">Hangup</ref>
		</see-also>
	</application>
 ***/

static void netblock_datastore_free(void *data)
{
	ast_free(data);
};

static const struct ast_datastore_info netblock_datastore_info = {
	.type = "network-blocking",
	.destroy = netblock_datastore_free,
};

static void handle_outgoing_response(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{
	const char *session_name = ast_sip_session_get_name(session);
	struct ast_channel *channel = session->channel;
	struct ast_datastore *ds;  /* The channel cleans up ds */

	struct pjsip_status_line *status_line;
	struct pj_str_t reason = { "Reason", 6 };

	/* should only operate on outgoing 603 responses to INVITE methods */

	SCOPE_ENTER(3, "%s\n", session_name);

	if(tdata->msg->type != PJSIP_RESPONSE_MSG) {
		SCOPE_EXIT_LOG_RTN(LOG_TRACE, "%s: Not a response.  Skipping.\n", session_name);
	}

	status_line = &tdata->msg->line.status;

	if(status_line->code != PJSIP_SC_DECLINE) {
		SCOPE_EXIT_LOG_RTN(LOG_TRACE, "%s: %d != %d response.  Skipping.\n", session_name, status_line->code, PJSIP_SC_DECLINE);
	}

	if (!channel) {
		SCOPE_EXIT_LOG_RTN(LOG_WARNING, "%s: Session has no channel...  Skipping.\n", session_name);
	}

	ast_channel_lock(channel);
	if (!(ds = ast_channel_datastore_find(channel, &netblock_datastore_info, NULL))) {
		ast_channel_unlock(channel);
		SCOPE_EXIT_LOG_RTN(LOG_TRACE, "%s: Channel has no network blocking datastore.  Skipping.\n",
			session_name);
	}
	ast_channel_unlock(channel);

	if(pjsip_msg_find_hdr_by_name(tdata->msg, &reason, NULL)) {
		ast_debug(1, "Found existing Reason header\n");
		ast_sip_remove_headers_by_name_and_value(tdata->msg, &reason, NULL);
	}

	pj_strdup2(tdata->pool, &status_line->reason, "Network Blocked");
	ast_sip_add_header(tdata, "Reason", (char *)ds->data);
}

enum netblock_flags {
	MUXFLAG_SIP = (1 << 0),
	MUXFLAG_Q850 = (1 << 1),
	MUXFLAG_LOCATION = (1 << 2),
	MUXFLAG_URL = (1 << 3),
	MUXFLAG_TEL = (1 << 4),
	MUXFLAG_EMAIL = (1 << 5),
	MUXFLAG_ID = (1 << 6),
};

enum netblock_args {
	OPT_ARG_LOCATION,
	OPT_ARG_URL,
	OPT_ARG_TEL,
	OPT_ARG_EMAIL,
	OPT_ARG_ID,
	OPT_ARG_ARRAY_SIZE, /* Always the last element of the enum */
};

AST_APP_OPTIONS(netblock_opts, {
	AST_APP_OPTION('s', MUXFLAG_SIP), /* will default to this one */
	AST_APP_OPTION('q', MUXFLAG_Q850),
	AST_APP_OPTION_ARG('l', MUXFLAG_LOCATION, OPT_ARG_LOCATION), /* will default to "RPN" */
	AST_APP_OPTION_ARG('u', MUXFLAG_URL, OPT_ARG_URL),
	AST_APP_OPTION_ARG('t', MUXFLAG_TEL, OPT_ARG_TEL),
	AST_APP_OPTION_ARG('e', MUXFLAG_EMAIL, OPT_ARG_EMAIL),
	AST_APP_OPTION_ARG('i', MUXFLAG_ID, OPT_ARG_ID),
});

static char *process_args(const char *data)
{
	struct ast_flags opts = { 0 };
	char *parse;
	char *opt_args[OPT_ARG_ARRAY_SIZE] = { NULL, };

	int reason_uri_found = 0;

	struct ast_str *value_buffer;

	parse = ast_strdupa(data);

	if (strlen(parse)) {
		value_buffer = ast_str_alloca(1024);

		ast_app_parse_options(netblock_opts, &opts, opt_args, parse);

		if (ast_test_flag(&opts, MUXFLAG_Q850)) {
			ast_str_append(&value_buffer, 0, "Q.850; cause=21; text=\"v=analytics1");
		} else {
			/* don't bother checking for SIP, it's the default if Q.850 is not specified */
			ast_str_append(&value_buffer, 0, "SIP; cause=603; text=\"v=analytics1");
		}
		if (ast_test_flag(&opts, MUXFLAG_URL)) {
			if (ast_strlen_zero(opt_args[OPT_ARG_URL])) {
				ast_log(LOG_WARNING, "Ignoring url option 'u': No value provided.\n");
			} else {
				reason_uri_found++;
				ast_str_append(&value_buffer, 0, ";url=%s", opt_args[OPT_ARG_URL]);
			}
		}
		if (ast_test_flag(&opts, MUXFLAG_TEL)) {
			if (ast_strlen_zero(opt_args[OPT_ARG_TEL])) {
				ast_log(LOG_WARNING, "Ignoring tel option 't': No value provided.\n");
			} else {
				reason_uri_found++;
				ast_str_append(&value_buffer, 0, ";tel=%s", opt_args[OPT_ARG_TEL]);
			}
		}
		if (ast_test_flag(&opts, MUXFLAG_EMAIL)) {
			if (ast_strlen_zero(opt_args[OPT_ARG_EMAIL])) {
				ast_log(LOG_WARNING, "Ignoring email option 'e': No value provided.\n");
			} else {
				reason_uri_found++;
				ast_str_append(&value_buffer, 0, ";email=%s", opt_args[OPT_ARG_EMAIL]);
			}
		}
		if (ast_test_flag(&opts, MUXFLAG_ID)) {
			if (ast_strlen_zero(opt_args[OPT_ARG_ID])) {
				ast_log(LOG_WARNING, "Ignoring id option 'i': No value provided.\n");
			} else {
				ast_str_append(&value_buffer, 0, ";id=%s", opt_args[OPT_ARG_ID]);
			}
		}
		if (ast_test_flag(&opts, MUXFLAG_LOCATION)) {
			if (ast_strlen_zero(opt_args[OPT_ARG_LOCATION])) {
				ast_log(LOG_WARNING, "Ignoring location option 'l': No value provided.\n");
				ast_str_append(&value_buffer, 0, "\";location=RPN");
			} else {
				ast_str_append(&value_buffer, 0, "\";location=%s", opt_args[OPT_ARG_LOCATION]);
			}
		} else {
			ast_log(LOG_WARNING, "No location value provided, using default value 'RPN'.\n");	
			ast_str_append(&value_buffer, 0, "\";location=RPN");
		}
	} else {
		ast_log(LOG_WARNING, "No options specified, 603+ processing disabled.\n");
		return(NULL);
	}

	if(reason_uri_found == 0) {
		ast_log(LOG_WARNING, "No valid uri/email/tel found, 603+ processing disabled.\n");
		return(NULL);
	}

	ast_debug(1, "Network Blocking: reason: %s\n", ast_str_buffer(value_buffer));

	return(ast_strdup(ast_str_buffer(value_buffer)));
}

static int netblock_app_exec(struct ast_channel *chan, const char *data)
{
	struct ast_datastore *datastore = NULL;

	if (ast_channel_state(chan) == AST_STATE_UP) {
		ast_log(AST_LOG_ERROR, "Network Blocking: channel already answered, cannot reject call!\n");
		return -1;
	}

	ast_channel_lock(chan);
	if ((datastore = ast_channel_datastore_find(chan, &netblock_datastore_info, NULL))) {
		ast_channel_unlock(chan);
		ast_debug(1, "Network Blocking already enabled on %s\n", ast_channel_name(chan));
	} else {
		ast_channel_unlock(chan);

		ast_debug(1, "Network Blocking no datastore found, setting up\n");

		/* Allocate a new datastore to hold the reference to tdd info */
		if (!(datastore = ast_datastore_alloc(&netblock_datastore_info, NULL))) {
			ast_log(AST_LOG_ERROR, "Network Blocking failed to create datastore!\n");
		} else {
			datastore->data = process_args(data);

			if(datastore->data && !ast_strlen_zero((char *)datastore->data)) {
				ast_channel_lock(chan);
				ast_channel_datastore_add(chan, datastore);
				ast_channel_unlock(chan);
			} else {
				ast_datastore_free(datastore);
			}
		}
	}

	ast_set_hangupsource(chan, "dialplan/NetworkBlocking", 0);
	ast_channel_lock(chan);

	/* ast_channel_hangupcause_set(chan, AST_CAUSE_CALL_REJECTED); <- results in 403 instead of 603 */
	ast_channel_hangupcause_set(chan, 0); /* <- results in 603 */
	ast_softhangup_nolock(chan, AST_SOFTHANGUP_EXPLICIT);

	ast_channel_unlock(chan);

	return -1;
}

static const char *app = "NetworkBlocking";

static struct ast_sip_session_supplement netblock_supplement = {
	.method = "INVITE",
	.outgoing_response = handle_outgoing_response,
};

static int unload_module(void) {
	int res = 0;

	ast_sip_session_unregister_supplement(&netblock_supplement);
	res = ast_unregister_application(app);
	return res;
}

static int load_module(void) {
	int res = 0;

	ast_sip_session_register_supplement(&netblock_supplement);
	res = ast_register_application_xml(app, netblock_app_exec);
	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Network Blocking 603+ application for Asterisk",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
	.requires = "res_pjsip,res_pjsip_session,chan_pjsip",
);
