/*** MODULEINFO
        <depend>kafka</depend>
        <support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "asterisk/res_kafka.h"

#include "asterisk/app.h"
#include "asterisk/config.h"
#include "asterisk/linkedlists.h"
#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"

#include <librdkafka/rdkafka.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static void rd_kafka_instance_destroy(void);
static void stop_pooling(void);

AST_MUTEX_DEFINE_STATIC(rk_lock);

static rd_kafka_t *rk;
static pthread_t rk_p_thread = AST_PTHREADT_NULL;
static volatile sig_atomic_t run_pooling = 1;
static const char *default_topic = NULL;

static void *producer_polling_thread(void *data) {
    ast_debug(3, "Producer polling thread started...\n");
    while (run_pooling) {
        int events = rd_kafka_poll(rk, 1000);

        if (events == 0 && !run_pooling) {
            break;
        }
    }
    ast_debug(3, "Producer polling thread stoped...\n");
    return NULL;
}

static void msg_dr_cb(rd_kafka_t *rkproducer, const rd_kafka_message_t *rkmessage, void *opaque) {
    if (rkmessage->err) {
        ast_log(LOG_ERROR, "Failed to delivery message: %s\n",
                rd_kafka_err2str(rkmessage->err));
    } else {
        ast_debug(3,
                  "Message delivery sucess (payload: %.*s)\n"
                  "(partition %d" PRId32 ")\n",
                  (int)rkmessage->len, (const char *)rkmessage->payload, rkmessage->partition);
    }
}

static void error_cb(rd_kafka_t *rk, int err, const char *reason, void *opaque) {
    ast_log(LOG_ERROR, "Error: %s: %s\n", rd_kafka_err2name(err), reason);
}

static void logger(const rd_kafka_t *r, int level, const char *fac, const char *buf) {
    ast_debug(3, "%i %s %s\n", level, fac, buf);
}

static int rd_kafka_instance_init() {
    rd_kafka_conf_t *conf;
    struct ast_config *cfg;
    char errstr[512];

    conf = rd_kafka_conf_new();

    if (!(cfg = read_config_file())) {
        return 1;
    }
    const char *default_topic_conf = ast_variable_retrieve(cfg, PRODUCER_CONF, TOPIC_CONF);
    
    if(default_topic_conf) {
        ast_debug(3,"Setting default topic %s\n", default_topic_conf);
        default_topic = default_topic_conf;
    }

    rd_kafka_conf_set_error_cb(conf, error_cb);
    rd_kafka_conf_set_log_cb(conf, logger);
    rd_kafka_conf_set_dr_msg_cb(conf, msg_dr_cb);

    if (parse_config(conf, cfg, PRODUCER_CONF) ||
        parse_config(conf, cfg, GENERAL_CONF) ||
        parse_config(conf, cfg, TOPIC_CONF)) return 1;

    ast_mutex_lock(&rk_lock);
    rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
    if (!rk) {
        rd_kafka_conf_destroy(conf);
        ast_log(LOG_ERROR, "Failed to create new producer: %s\n",
                errstr);
        return 1;
    }
    ast_mutex_unlock(&rk_lock);

    if (ast_pthread_create_background(&rk_p_thread, NULL, producer_polling_thread, NULL)) {
        ast_log(LOG_ERROR, "Cannot start producer pooling thread");
        rd_kafka_instance_destroy();
        return 1;
    }

    return 0;
}

static void stop_pooling() {
    run_pooling = 0;
    if (rk_p_thread != AST_PTHREADT_NULL) {
        pthread_kill(rk_p_thread, SIGURG);
        pthread_join(rk_p_thread, NULL);

        rk_p_thread = AST_PTHREADT_NULL;
    }
}

static void rd_kafka_instance_destroy() {
    stop_pooling();
    ast_debug(3, "Flushing messages...\n");

    ast_mutex_lock(&rk_lock);

    rd_kafka_flush(rk, 5 * 1000);
    rd_kafka_destroy(rk);

    ast_mutex_unlock(&rk_lock);
}

static int kafka_producer_exec(struct ast_channel *chan, const char *vargs) {
    char *data;
    rd_kafka_resp_err_t err;

    AST_DECLARE_APP_ARGS(args,
                         AST_APP_ARG(topic);
                         AST_APP_ARG(key);
                         AST_APP_ARG(msg););

    data = ast_strdupa(vargs);

    if (ast_strlen_zero(data)) {
        ast_log(LOG_WARNING, "%s requires an argument (topic, key, message)\n", "ProducerSend");
        return -1;
    }
    AST_STANDARD_APP_ARGS(args, data);

    bool topic_not_provided = ast_strlen_zero(args.topic) && ast_strlen_zero(default_topic);

    if (topic_not_provided) {
        ast_log(LOG_WARNING, "Not topic provided");
        return -1;
    }

    if (chan) {
        ast_autoservice_start(chan);
    }
    ast_mutex_lock(&rk_lock);

    const char *topic = ast_strlen_zero(args.topic) ? default_topic : args.topic;

    ast_debug(1, "sending message: \"%s\" with key: \"%s\", to topic: \"%s\"", args.msg, args.key, topic);

    err = rd_kafka_producev(
        rk, RD_KAFKA_V_TOPIC(topic),
        RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
        RD_KAFKA_V_KEY(args.key, strlen(args.key)),
        RD_KAFKA_V_VALUE(args.msg, strlen(args.msg)),
        RD_KAFKA_V_END);

    if (err) {
        ast_log(LOG_ERROR, "FAILED TO DELIVERY MESSAGE %s\n", rd_kafka_err2str(err));
    } else {
        ast_debug(3, "Enqueued message: %s\n", args.msg);
    }

    ast_mutex_unlock(&rk_lock);

    if (chan) {
        ast_autoservice_stop(chan);
    }

    return 0;
}

int load_module(void) {
    int res = 0;

    res = res || rd_kafka_instance_init();
    res = res || ast_register_application("ProducerSend", kafka_producer_exec, "todo", "todo");

    return res;
}

int unload_module(void) {
    int res = 0;

    rd_kafka_instance_destroy();

    res |= ast_unregister_application("ProducerSend");

    return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Dialplan Kafka Applications and Functions",
                .load = load_module,
                .unload = unload_module);
