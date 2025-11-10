#include "asterisk.h"

#include "asterisk/config.h"
#include "asterisk/app.h"
#include "asterisk/linkedlists.h"
#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/res_kafka.h"


int parse_config(rd_kafka_conf_t *conf, struct ast_config *cfg, char *category) {
    struct ast_variable *var;
    char errstr[512];

    ast_debug(3, "Loading config category: %s", category);
    for (var = ast_variable_browse(cfg, category); var; var = var->next) {
        if (!strcasecmp(var->name, "topic"))
            continue;
        ast_debug(3, "Setting config %s with value %s\n", var->name, var->value);

        if (rd_kafka_conf_set(conf, var->name, var->value, errstr,
                              sizeof(errstr)) != RD_KAFKA_CONF_OK) {
            rd_kafka_conf_destroy(conf);
            ast_log(LOG_ERROR, "Error on define conf. Aborting. %s\n", errstr);
            return 1;
        }
    }
    return 0;
}

struct ast_config *read_config_file() {
    struct ast_config *cfg;
    struct ast_flags config_flags = {0};

    cfg = ast_config_load(KAFKA_CONF_FILE, config_flags);

    if (!cfg) {
        ast_log(LOG_ERROR, "Error reading config file: %s\n", KAFKA_CONF_FILE);
        return NULL;
    } else if (cfg == CONFIG_STATUS_FILEINVALID) {
        ast_log(LOG_ERROR, "Config file " KAFKA_CONF_FILE " is in an invalid format. Aborting.\n");
        return NULL;
    }

    return cfg;
}
