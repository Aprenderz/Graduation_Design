#include "mqtt_client.h"
#include "at_driver.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static MQTT_State_t s_mqtt_state = MQTT_STATE_DISCONNECTED;
static mqtt_msg_callback_t s_msg_callback = NULL;

static void on_mqtt_message(const char *line)
{
    if (!s_msg_callback) return;

    // 跳过 "+MSUB: \""
    const char *prefix = "+MSUB: \"";
    if (strncmp(line, prefix, strlen(prefix)) != 0) return;

    const char *p = line + strlen(prefix);

    // 提取 Topic（直到下一个双引号）
    const char *topic_start = p;
    const char *topic_end = strchr(p, '\"');
    if (!topic_end) return;
    size_t topic_len = topic_end - topic_start;
    if (topic_len >= MQTT_TOPIC_MAX_LEN) topic_len = MQTT_TOPIC_MAX_LEN - 1;

    char topic[topic_len];
    memcpy(topic, topic_start, topic_len);
    topic[topic_len] = '\0';

    // 跳过双引号和逗号，定位到 payload 起始
    p = topic_end + 1;      // 跳过结尾的 "
    p = strchr(p, ',');     // 找到第一个逗号（长度描述前）
    if (!p) return;
    p++;                    // 跳过逗号

    // 跳过长度描述字段（例如 "75 byte"），再找下一个逗号
    p = strchr(p, ',');
    if (!p) return;
    p++;                    // 跳过逗号，此时 p 指向 JSON payload 开头

    // payload 直到行尾（去除尾部换行）
    size_t payload_len = strlen(p);
    while (payload_len > 0 && (p[payload_len-1] == '\r' || p[payload_len-1] == '\n'))
        payload_len--;

    if (payload_len >= MQTT_MSG_MAX_LEN) payload_len = MQTT_MSG_MAX_LEN - 1;

    char payload[payload_len];
    memcpy(payload, p, payload_len);
    payload[payload_len] = '\0';

    // 调用上层回调，传递 Topic 和 Payload
    s_msg_callback(topic, payload, (uint16_t)payload_len);
}

void MQTT_Init(mqtt_msg_callback_t callback)
{
    s_msg_callback = callback;
    s_mqtt_state = MQTT_STATE_DISCONNECTED;
    AT_RegisterURC("+MSUB:", on_mqtt_message);
    AT_MQTT_Close();
}

bool MQTT_EnsureNetworkActive(void)
{
    char ip[32];
    if (!AT_EnsureGPRSAttach()) return false;
    if (AT_GetIPAddress(ip, sizeof(ip))) return true;
    if (!AT_SetAPN()) return false;
    if (!AT_ActivateNetwork()) return false;
    osDelay(500);
    if (!AT_GetIPAddress(ip, sizeof(ip))) return false;
    return true;
}

static bool mqtt_establish(const MQTT_Config_t *config)
{
    uint16_t port_num;
    if (!AT_MQTT_Config(config->client_id, config->username, config->password)) return false;
    port_num = (uint16_t)atoi(config->port);
    if (!AT_MQTT_Start(config->server, port_num)) return false;
    if (!AT_MQTT_Connect_Wait(1, 60, 10000)) {
        AT_MQTT_Close();
        return false;
    }
    s_mqtt_state = MQTT_STATE_CONNECTED;
    return true;
}

bool MQTT_Connect(const MQTT_Config_t *config)
{
    if (!MQTT_EnsureNetworkActive()) return false;
    return mqtt_establish(config);
}

void MQTT_FullDisconnect(void)
{
    if (s_mqtt_state == MQTT_STATE_CONNECTED) {
        AT_MQTT_Disconnect();   // 发送 AT+MDISCONNECT
        osDelay(200);
        AT_MQTT_Close();        // 发送 AT+MIPCLOSE
    }
    s_mqtt_state = MQTT_STATE_DISCONNECTED;
}
bool MQTT_Publish(const char *topic, uint8_t qos, uint8_t retain, const char *payload)
{
    if (s_mqtt_state != MQTT_STATE_CONNECTED)   return false;
    return AT_MQTT_Publish(topic, qos, retain, payload);
}

bool MQTT_Subscribe(const char *topic, uint8_t qos)
{
    if (s_mqtt_state != MQTT_STATE_CONNECTED)   return false;
    return AT_MQTT_Subscribe(topic, qos);
}

MQTT_State_t MQTT_GetState(void)
{
    return s_mqtt_state;
}