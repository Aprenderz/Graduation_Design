#ifndef TASK_COMM_H
#define TASK_COMM_H

#include <stdbool.h>
#include "mqtt_client.h" 

// 下行命令消息（URC 回调只负责填充 topic 和 payload）
typedef struct {
    char topic[MQTT_TOPIC_MAX_LEN];
    char payload[MQTT_MSG_MAX_LEN];
} downlink_msg_t;

void CommTask(void *argument);

#endif