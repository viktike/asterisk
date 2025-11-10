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


AST_MUTEX_DEFINE_STATIC(rk_lock);

static rd_kafka_t *rk;
static pthread_t rk_thread = AST_PTHREADT_NULL;
static volatile sig_atomic_t run_pooling = 1;

int load_module(void);
int unload_module(void);

static void handle_reply(char *msg, rd_kafka_headers_t *hdrs);

static void *consumer_polling_thread(void *data) {
    ast_debug(3, "Consumer polling thread started...\n");
    while (run_pooling) {
        rd_kafka_message_t *rkm;

        rkm = rd_kafka_consumer_poll(rk, 100);

        if (!rkm)
            continue;

        if (rkm->err) {
            ast_log(LOG_ERROR, "Consumer Error: %s\n", rd_kafka_message_errstr(rkm));
            if(rkm->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
               rkm->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC ||
               rkm->err == RD_KAFKA_RESP_ERR_UNKNOWN_TOPIC_OR_PART) {
                rd_kafka_message_destroy(rkm);
                run_pooling = 0;
                break;
            }
            rd_kafka_message_destroy(rkm);
            continue;
        }

        ast_debug(3, "Message on %s [%" PRId32 "] at offset %" PRId64,
                  rd_kafka_topic_name(rkm->rkt), rkm->partition,
                  rkm->offset);

        if (rkm->key)
            ast_debug(3, " Key: %.*s\n", (int)rkm->key_len, (const char *)rkm->key);

        if (rkm->payload) {
            rd_kafka_headers_t *hdrs;

            if (!rd_kafka_message_headers(rkm, &hdrs)) {
                int len = (int)rkm->len;
                const char *msg = (const char *)rkm->payload;
                char buf[4096];

                buf[0] = '\0';
                snprintf(buf, sizeof(buf), "%.*s", len, msg);

                handle_reply(buf, hdrs);
            }
        }
        rd_kafka_message_destroy(rkm);
    }
    ast_debug(3, "Consumer polling thread stoped...\n");
    return NULL;
}

static void handle_reply(char *msg, rd_kafka_headers_t *hdrs) {
    rd_kafka_resp_err_t err;
    const void *value = NULL;
    size_t size;

    err = rd_kafka_header_get_last(hdrs, "channel", &value, &size);

    if (err) {
        ast_log(LOG_ERROR, "HEADER Error: %s\n", rd_kafka_err2str(err));
        return;
    }

    if (value) {
        const char *channel = (const char *)value;
        struct ast_channel *chan = ast_channel_get_by_name(channel);

        if (chan) {
            ast_debug(3, "SETING VAR: %s SIZE: %zu", msg, size);
            pbx_builtin_setvar_helper(chan, "PRODUCERMSG", msg);
            chan = ast_channel_unref(chan);
        } else {
            ast_log(LOG_ERROR, "Channel: %s not found!", channel);
        }
    }
}
static void error_cb(rd_kafka_t *rk, int err, const char *reason, void *opaque) {
    ast_log(LOG_ERROR, "Error: %s: %s\n", rd_kafka_err2name(err), reason);
}

static void logger(const rd_kafka_t *r, int level, const char *fac, const char *buf) {
    ast_debug(3, "%i %s %s\n", level, fac, buf);
}

static int rd_kafka_consumer_new() {
    rd_kafka_conf_t *conf;
    struct ast_config *cfg;
    char errstr[512];
    char *topic = NULL;
    char *topics = NULL;
    rd_kafka_topic_partition_list_t *subscription;
    rd_kafka_resp_err_t err;

    conf = rd_kafka_conf_new();

    if (!(cfg = read_config_file())) {
        return 1;
    }

    rd_kafka_conf_set_error_cb(conf, error_cb);
    rd_kafka_conf_set_log_cb(conf, logger);

    const char *topics_config = ast_variable_retrieve(cfg, CONSUMER_CONF, TOPIC_CONF);

    if(!topics_config) {
        ast_debug(3, "List of topics missing, try adding in the kafka.conf file in the [consumer] session");
        return 1;
    }

    if (parse_config(conf, cfg, CONSUMER_CONF) ||
        parse_config(conf, cfg, GENERAL_CONF) ||
        parse_config(conf, cfg, TOPIC_CONF)) return 1;


    unsigned int topics_count = 0;
    char *topics_arr[MAX_TOPIC_CONSUMER];

    topics = ast_strdupa(topics_config);
    while ((topic = strsep(&topics, ","))) {
        topic = ast_strip(topic);

        if (topics_count > MAX_TOPIC_CONSUMER) break;
        topics_arr[topics_count++] = topic;

        ast_debug(3, "TOPIC ADDED: %s", topic);
    }
    ast_mutex_lock(&rk_lock);
    rk = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
    if (!rk) {
        rd_kafka_conf_destroy(conf);
        ast_log(LOG_ERROR, "Failed to create new consumer: %s\n",
                errstr);
        return 1;
    }

    conf = NULL;

    rd_kafka_poll_set_consumer(rk);
    subscription = rd_kafka_topic_partition_list_new(topics_count);
    if (topics_count) {
        for (int i = 0; i < topics_count; i++) {
            rd_kafka_topic_partition_list_add(subscription, topics_arr[i], RD_KAFKA_PARTITION_UA);
        }
    }

    err = rd_kafka_subscribe(rk, subscription);
    if (err) {
        ast_log(LOG_ERROR, "Failed to subscribe to %d topics: %s",
                subscription->cnt, rd_kafka_err2str(err));
        rd_kafka_topic_partition_list_destroy(subscription);
        rd_kafka_destroy(rk);
        return 1;
    }

    ast_debug(3,
              "Subscribed to %d topic(s), "
              "waiting for rebalance and messages...",
              subscription->cnt);

    rd_kafka_topic_partition_list_destroy(subscription);

    ast_mutex_unlock(&rk_lock);

    if (ast_pthread_create_background(&rk_thread, NULL, consumer_polling_thread, NULL)) {
        ast_log(LOG_ERROR, "Cannot start producer pooling thread");
        rd_kafka_consumer_close(rk);
        rd_kafka_destroy(rk);
        return 1;
    }

    return 0;
}

static void rd_kafka_consumer_destroy() {
    run_pooling = 0;
    if (rk_thread != AST_PTHREADT_NULL) {
        ast_debug(3, "Destroying polling thread...\n");
        pthread_kill(rk_thread, SIGURG);
        pthread_join(rk_thread, NULL);

        rk_thread = AST_PTHREADT_NULL;
    }
    if (rk) {
        ast_debug(3, "Closing consumer...\n");
        ast_mutex_lock(&rk_lock);

        rd_kafka_consumer_close(rk);
        rd_kafka_destroy(rk);

        ast_mutex_unlock(&rk_lock);

        ast_debug(3, "Finish\n");
    }
}

int load_module(void) {
    int res = 0;

    res = rd_kafka_consumer_new();

    return res;
}

int unload_module(void) {
    rd_kafka_consumer_destroy();
    return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Dialplan Kafka Applications and Functions",
                .load = load_module,
                .unload = unload_module);
