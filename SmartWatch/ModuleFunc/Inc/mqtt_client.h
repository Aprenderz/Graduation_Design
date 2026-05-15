#ifndef __MQTT_CLIENT_H__
#define __MQTT_CLIENT_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_TOPIC_MAX_LEN  256
#define MQTT_MSG_MAX_LEN    516

typedef enum {
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_ERROR
} MQTT_State_t;

typedef struct {
    const char *server;
    const char *port;
    const char *client_id;
    const char *username;
    const char *password;
} MQTT_Config_t;

typedef void (*mqtt_msg_callback_t)(const char *topic, const char *payload, uint16_t len);

void MQTT_Init(mqtt_msg_callback_t callback);
bool MQTT_EnsureNetworkActive(void);      // 确保 PDP 激活（获取 IP）
bool MQTT_Connect(const MQTT_Config_t *config);
void MQTT_FullDisconnect(void);           // 完全断开 MQTT 并释放 TCP
bool MQTT_Publish(const char *topic, uint8_t qos, uint8_t retain, const char *payload);
bool MQTT_Subscribe(const char *topic, uint8_t qos);
MQTT_State_t MQTT_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* __MQTT_CLIENT_H__ */