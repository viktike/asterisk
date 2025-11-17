#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/module.h"
//#include "asterisk/strings.h"
#include "asterisk/pbx.h"

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_session</depend>
	<support_level>core</support_level>
 ***/

static int set_transport_variable(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	const char *transport_id;

	if (!ast_begins_with(rdata->tp_info.transport->info, AST_SIP_X_AST_TXP ":")) {
		return PJ_FALSE;
	}

	transport_id = rdata->tp_info.transport->info + AST_SIP_X_AST_TXP_LEN + 1;
	pbx_builtin_setvar_helper(session->channel, "TRANSPORT_ID", transport_id);
	return PJ_TRUE;
}

static int pbxhelper_incoming_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
    set_transport_variable(session, rdata);

    return 0;
}

static struct ast_sip_session_supplement pbxhelper_supplement = {
    .method = "INVITE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_LAST,
    //.outgoing_request = auto_answer_outgoing_request,
    .incoming_request = pbxhelper_incoming_request,
};

static int load_module(void)
{
    ast_sip_session_register_supplement(&pbxhelper_supplement);
    return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
    ast_sip_session_unregister_supplement(&pbxhelper_supplement);
    return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PBX Helper",
    .support_level = AST_MODULE_SUPPORT_CORE,
    .load = load_module,
    .unload = unload_module,
    .load_pri = AST_MODPRI_APP_DEPEND,
);
