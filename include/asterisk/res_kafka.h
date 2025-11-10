#ifndef RES_KAFKA_CONFIG_H
#define RES_KAFKA_CONFIG_H

#define KAFKA_CONF_FILE "kafka.conf"
#define GENERAL_CONF "general"
#define PRODUCER_CONF "producer"
#define TOPIC_CONF "topic"
#define CONSUMER_CONF "consumer"
#define MAX_TOPIC_CONSUMER 10

#include <librdkafka/rdkafka.h>
#include "asterisk/config.h"

struct ast_config *read_config_file();

int parse_config(rd_kafka_conf_t *conf, struct ast_config *cfg, char *category);

#endif /* RES_KAFKA_CONFIG_H  */
