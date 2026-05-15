#include "task_comm.h"
#include "mqtt_client.h"
#include "main.h"
#include "at_driver.h"
#include "phone.h"
#include "sms.h"
#include "vibrate.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MQTT_PUBLISH_INTERVAL_MS    5000
#define MQTT_RECONNECT_DELAY_MS     5000

// 华为云 IoTDA 参数（请根据实际修改）
#define CLOUD_SERVER   "2966810d56.st1.iotda-device.cn-south-1.myhuaweicloud.com"
#define CLOUD_PORT     "1883"
#define CLIENT_ID      "69c8f536c00ccb6d4b47abb9_869701076094260_0_0_2026040302"
#define USERNAME       "69c8f536c00ccb6d4b47abb9_869701076094260"
#define PASSWORD       "6ac34507acf8af0d072bfbd9b6837ec298362a47914c9b5d7cb872ad1a72bff1"

#define SUBSCRIBE_TOPIC_CMD     "$oc/devices/" USERNAME "/sys/commands/#"
// #define SUBSCRIBE_TOPIC_PROP    "$oc/devices/" USERNAME "/sys/properties/set/#"  // 不订阅属性设置主题，也能收到，隐式订阅
#define PUBLISH_TOPIC           "$oc/devices/" USERNAME "/sys/properties/report"

static const MQTT_Config_t mqtt_cfg = {
    .server = CLOUD_SERVER,
    .port = CLOUD_PORT,
    .client_id = CLIENT_ID,
    .username = USERNAME,
    .password = PASSWORD,
};

/* ------- JSON 转义 ------- */
static void at_escape_json(const char *src, char *dst, size_t dst_size)
{
    size_t i = 0, j = 0;
    while (src[i] != '\0' && j < dst_size - 5) {
        if (src[i] == '"') {
            dst[j++] = '\\';
            dst[j++] = '\\';
            dst[j++] = '2';
            dst[j++] = '2';
        } else {
            dst[j++] = src[i];
        }
        i++;
    }
    dst[j] = '\0';
}

static bool get_sensor_snapshot(SensorData_t *sensor, SystemStatus_t *status)
{
    if (osMutexAcquire(xDataMutexHandle, 100) != osOK)
        return false;

    *sensor = g_SensorData;
    *status = g_SystemStatus;

    osMutexRelease(xDataMutexHandle);
    return true;
}

static bool report_sensor_data(const SensorData_t *sensor, const SystemStatus_t *status)
{
    char payload_raw[MQTT_MSG_MAX_LEN];
    char payload_escaped[MQTT_MSG_MAX_LEN * 2];
    bool success = false;

    const char *alarm_str = (status->alarm_reason[0] != '\0') ? status->alarm_reason : "Normal";

    snprintf(payload_raw, sizeof(payload_raw),
        "{\"services\":[{\"service_id\":\"GPS_BD\",\"properties\":{\"longitude\":%.6f,\"latitude\":%.6f}},{\"service_id\":\"Health\",\"properties\":{\"heart_rate\":%d,\"spo2\":%d,\"temp\":%.2f,\"step\":%lu}},{\"service_id\":\"Alarm\",\"properties\":{\"alarm_status\":\"%s\"}}]}",
        sensor->longitude, sensor->latitude,
        sensor->heart_rate, sensor->spo2, sensor->temperature, sensor->step,
        alarm_str
    );

    at_escape_json(payload_raw, payload_escaped, sizeof(payload_escaped));

    if (MQTT_Publish(PUBLISH_TOPIC, 1, 0, payload_escaped)) {
        osDelay(1000);
        success = true;
    }
    return success;
}

static void reset_alert_state(void)
{
    if (osMutexAcquire(xDataMutexHandle, 100) == osOK) {
        g_SystemStatus.need_upload = false;
        if (g_SystemStatus.state != SYS_STATE_NORMAL) {
            g_SystemStatus.state = SYS_STATE_NORMAL;
            g_SystemStatus.alarm_reason[0] = '\0';
        }
        osMutexRelease(xDataMutexHandle);
    }
}

// 辅助函数：从 payload 中提取 phone 和 addr（简单字符串查找）
static bool parse_family_properties(const char *payload, char *phone_out, size_t phone_size,
                                    char *addr_out, size_t addr_size)
{
    bool found_phone = false, found_addr = false;
    const char *p;

    // 提取 "phone":"..."
    p = strstr(payload, "\"phone\":\"");
    if (p) {
        p += 9; // 跳过 "phone":"
        const char *end = strchr(p, '\"');
        if (end) {
            size_t len = end - p;
            if (len < phone_size) {
                memcpy(phone_out, p, len);
                phone_out[len] = '\0';
                found_phone = true;
            }
        }
    }

    // 提取 "addr":"..."
    p = strstr(payload, "\"addr\":\"");
    if (p) {
        p += 8; // 跳过 "addr":"
        const char *end = strchr(p, '\"');
        if (end) {
            size_t len = end - p;
            if (len < addr_size) {
                memcpy(addr_out, p, len);
                addr_out[len] = '\0';
                found_addr = true;
            }
        }
    }

    return (found_phone || found_addr);
}

// 从 Topic 中提取 request_id
static bool extract_request_id(const char *topic, char *request_id, size_t size)
{
    const char *key = "request_id=";
    const char *start = strstr(topic, key);
    if (!start) return false;
    start += strlen(key);
    const char *end = start;
    while (*end && *end != '&' && *end != '/') end++;
    size_t len = end - start;
    if (len >= size) len = size - 1;
    memcpy(request_id, start, len);
    request_id[len] = '\0';
    return true;
}

// 下行命令入队回调（由 MQTT URC 调用）
static void handle_downlink_command(const char *topic, const char *payload, uint16_t len)
{
    downlink_msg_t msg;
    strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
    msg.topic[sizeof(msg.topic) - 1] = '\0';
    strncpy(msg.payload, payload, sizeof(msg.payload) - 1);
    msg.payload[sizeof(msg.payload) - 1] = '\0';

    osMessageQueuePut(downlinkQueueHandle, &msg, 0, 0);
}

// 在通信任务中处理队列中的下行命令
static void process_downlink_command(const char *topic, const char *payload)
{
    char phone[20] = {0};
    char addr[128] = {0};

    // 1. 解析 JSON，获取 phone / addr
    if (!parse_family_properties(payload, phone, sizeof(phone), addr, sizeof(addr))) {
        return;
    }

    // 2. 更新全局配置
    if (osMutexAcquire(xDataMutexHandle, 100) == osOK) {
        if (strlen(phone) > 0) {
            strncpy(g_FamilyConfig.phone, phone, sizeof(g_FamilyConfig.phone)-1);
            g_FamilyConfig.valid = true;
        }
        if (strlen(addr) > 0) {
            strncpy(g_FamilyConfig.address, addr, sizeof(g_FamilyConfig.address)-1);
            g_FamilyConfig.valid = true;
        }
        osMutexRelease(xDataMutexHandle);
    }

    // 3. 提取 request_id，构造响应 Topic
    char request_id[64] = {0};
    if (!extract_request_id(topic, request_id, sizeof(request_id))) {
        return;
    }

    char resp_topic[256];
    snprintf(resp_topic, sizeof(resp_topic),
             "$oc/devices/%s/sys/properties/set/response/request_id=%s",
             USERNAME, request_id);

    // 4. 发送响应 (result_code:0)
    char resp_payload[64] = "{\"result_code\":0}";
    char resp_escaped[128];
    at_escape_json(resp_payload, resp_escaped, sizeof(resp_escaped));
    AT_MQTT_Publish(resp_topic, 0, 0, resp_escaped);

    // 5. 上报更新后的属性（可选，但推荐）
    char report_raw[MQTT_MSG_MAX_LEN];
    char report_escaped[MQTT_MSG_MAX_LEN * 2];
    bool has_phone = (strlen(g_FamilyConfig.phone) > 0);
    bool has_addr = (strlen(g_FamilyConfig.address) > 0);

    if (has_phone && has_addr) {
        snprintf(report_raw, sizeof(report_raw),
                 "{\"services\":[{\"service_id\":\"Family\",\"properties\":{\"phone\":\"%s\",\"addr\":\"%s\"}}]}",
                 g_FamilyConfig.phone, g_FamilyConfig.address);
    } else if (has_phone) {
        snprintf(report_raw, sizeof(report_raw),
                 "{\"services\":[{\"service_id\":\"Family\",\"properties\":{\"phone\":\"%s\"}}]}",
                 g_FamilyConfig.phone);
    } else if (has_addr) {
        snprintf(report_raw, sizeof(report_raw),
                 "{\"services\":[{\"service_id\":\"Family\",\"properties\":{\"addr\":\"%s\"}}]}",
                 g_FamilyConfig.address);
    } else {
        return;
    }

    at_escape_json(report_raw, report_escaped, sizeof(report_escaped));
    AT_MQTT_Publish(PUBLISH_TOPIC, 0, 0, report_escaped);
}

/* ------- 主通信任务 ------- */
void CommTask(void *argument)
{
    uint32_t last_publish = 0;
    uint32_t last_reconnect_attempt = 0;
    bool mqtt_connected = false;
    uint8_t publish_fail_count = 0;
    downlink_msg_t downlink_msg;
    SensorData_t sensor_snap;
    SystemStatus_t status_snap;

    AT_Driver_Init();
    Phone_Init();
    SMS_Init();
    MQTT_Init(handle_downlink_command);

    osDelay(1000);
    while (1) {

        if (osSemaphoreAcquire(xCommPauseSemHandle, osWaitForever) != osOK) continue;

        uint32_t now = HAL_GetTick();

        /* ----- 1. MQTT 连接管理（含订阅） ----- */
        if (!mqtt_connected && (now - last_reconnect_attempt) >= MQTT_RECONNECT_DELAY_MS) {
            MQTT_FullDisconnect();
            if (MQTT_Connect(&mqtt_cfg)) {
                if (MQTT_Subscribe(SUBSCRIBE_TOPIC_CMD, 1)) {
                    osDelay(1000);
                    mqtt_connected = true;
                    publish_fail_count = 0;
                } else {
                    MQTT_FullDisconnect();
                }
            }
            last_reconnect_attempt = now;
        }

        /* ----- 2. 处理下行命令队列 ----- */
        if (mqtt_connected) {
            while (osMessageQueueGet(downlinkQueueHandle, &downlink_msg, NULL, 0) == osOK) {
                process_downlink_command(downlink_msg.topic, downlink_msg.payload);
            }
        }

        /* ----- 3. 定期上报传感器数据 ----- */
        if (get_sensor_snapshot(&sensor_snap, &status_snap)) {
            if (mqtt_connected) {
                if (status_snap.need_upload || (now - last_publish) >= MQTT_PUBLISH_INTERVAL_MS) {
                    if (report_sensor_data(&sensor_snap, &status_snap)) {
                        publish_fail_count = 0;
                        last_publish = now;
                        reset_alert_state();
                    } else {
                        if (!status_snap.need_upload) {
                            publish_fail_count++;
                            if (publish_fail_count >= 3) {
                                mqtt_connected = false;
                                publish_fail_count = 0;
                                osDelay(2000);
                                MQTT_FullDisconnect();
                                continue;
                            }
                        }
                    }
                }
            }
            // 无论如何，只要 need_upload 持续超过 1 分钟，强制重置
            if (status_snap.need_upload && (HAL_GetTick() - status_snap.alarm_timestamp) >= 60000) {
                reset_alert_state();
            }
        }

        /* ----- 4. 状态检查 ----- */
        if (mqtt_connected && MQTT_GetState() != MQTT_STATE_CONNECTED) {
            mqtt_connected = false;
            publish_fail_count = 0;
        }

        osSemaphoreRelease(xCommPauseSemHandle);
        osDelay(200);
    }
}