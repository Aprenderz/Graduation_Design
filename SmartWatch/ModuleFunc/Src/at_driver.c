#include "at_driver.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static char s_resp_buf[512];
static uint16_t s_resp_len = 0;
static osMutexId_t s_uart_tx_mutex;            // 串口发送互斥锁
static osMutexId_t s_resp_mutex = NULL;        // 响应互斥锁
static osSemaphoreId_t s_cmd_sem = NULL;       //等待OK/ERROR 
static osSemaphoreId_t s_mqtt_sem = NULL;      //等待 MQTT CONNACK
static osSemaphoreId_t s_connect_sem = NULL;   // 等待CONNECT OK/FAIL
static bool s_connect_ok = false;
static bool s_mqtt_conn_ok = false;

#define MAX_URC_CALLBACKS  8
typedef struct {
    const char *prefix;
    void (*callback)(const char *line);
} urc_callback_t;

static urc_callback_t s_urc_callbacks[MAX_URC_CALLBACKS];
static uint8_t s_urc_count = 0;

static char s_rx_line_buf[512];
static uint16_t s_rx_line_idx = 0;

extern osSemaphoreId_t s_call_connect_sem;

void AT_RegisterURC(const char *prefix, void (*callback)(const char *line))
{
    if (s_urc_count < MAX_URC_CALLBACKS) {
        s_urc_callbacks[s_urc_count].prefix = prefix;
        s_urc_callbacks[s_urc_count].callback = callback;
        s_urc_count++;
    }
}

static void on_mqtt_connect(const char *line)
{
    if (strstr(line, "CONNECT OK") != NULL) {
        s_connect_ok = true;
        if (s_connect_sem) osSemaphoreRelease(s_connect_sem);
    } else if (strstr(line, "CONNECT FAIL") != NULL) {
        s_connect_ok = false;
        if (s_connect_sem) osSemaphoreRelease(s_connect_sem);
    } else if (strstr(line, "ALREADY CONNECT") != NULL) {
        s_connect_ok = true;
        if (s_connect_sem) osSemaphoreRelease(s_connect_sem);
    }else if (strcmp(line, "CONNECT") == 0) {
        if (s_call_connect_sem) {
            osSemaphoreRelease(s_call_connect_sem);
        }
    }
}

static void on_mqtt_connack(const char *line)
{
    if (strstr(line, "CONNACK OK") != NULL) {
        s_mqtt_conn_ok = true;
        if (s_mqtt_sem) osSemaphoreRelease(s_mqtt_sem);
    }
}

static void ProcessLine(const char *line)
{
    if (line[0] == '\0') return;

    for (uint8_t i = 0; i < s_urc_count; i++) {
        if (strncmp(line, s_urc_callbacks[i].prefix, strlen(s_urc_callbacks[i].prefix)) == 0) {
            if (s_urc_callbacks[i].callback) {
                s_urc_callbacks[i].callback(line);
            }
            return;
        }
    }

    if (s_cmd_sem != NULL) {
        if (s_resp_mutex) osMutexAcquire(s_resp_mutex, portMAX_DELAY);
        strncpy(s_resp_buf, line, sizeof(s_resp_buf)-1);
        s_resp_buf[sizeof(s_resp_buf)-1] = '\0';
        s_resp_len = strlen(line);
        if (s_resp_len > 0 && s_resp_buf[s_resp_len-1] == '\r') s_resp_len--;
        if (s_resp_len > 0 && s_resp_buf[s_resp_len-1] == '\n') s_resp_len--;
        if (s_resp_mutex) osMutexRelease(s_resp_mutex);

        if (strstr(line, "OK") || strstr(line, "ERROR")) {
            osSemaphoreRelease(s_cmd_sem);
        }
    }
}

void URC_Task(void *argument)
{
    uart_msg_t msg;
    while (1) {
        if (xQueueReceive(xUartRxQueueHandle, &msg, portMAX_DELAY) == pdPASS) {
            msg.data[msg.len] = '\0';
            ProcessLine(msg.data);
        }
    }
}

void AT_Driver_Init(void)
{
    s_uart_tx_mutex = osMutexNew(NULL);
    s_resp_mutex = osMutexNew(NULL);
    s_cmd_sem = osSemaphoreNew(1, 0, NULL);
    s_mqtt_sem = osSemaphoreNew(1, 0, NULL);
    s_connect_sem = osSemaphoreNew(1, 0, NULL);   // 新增

    AT_RegisterURC("CONNECT", on_mqtt_connect);   // 处理 AT+MIPSTART 响应、CONNECT OK/FAIL
    AT_RegisterURC("ALREADY", on_mqtt_connect);  // 处理 ALREADY CONNECT
    AT_RegisterURC("CONNACK", on_mqtt_connack);   // 处理 AT+MCONNECT 响应

    HAL_UART_Receive_IT(&huart1, (uint8_t*)&s_rx_line_buf[0], 1);
    AT();
}

/**
 * @brief 线程安全的原始数据发送（阻塞，带超时）
 * @param data   待发送数据
 * @param len    数据长度
 * @param timeout_ms 超时时间（毫秒）
 * @return true 发送成功，false 超时或硬件错误
 */
bool AT_SendRaw(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    if (osMutexAcquire(s_uart_tx_mutex, pdMS_TO_TICKS(timeout_ms)) != osOK) {
        return false;
    }
    HAL_StatusTypeDef ret = HAL_UART_Transmit(&huart1, (uint8_t *)data, len, timeout_ms);
    osMutexRelease(s_uart_tx_mutex);
    return (ret == HAL_OK);
}

void AT(void)
{
    AT_SendCommand("AT", 1000);
    osDelay(100);
}

void AT_ClearRespBuffer(void)
{
    if (s_resp_mutex) osMutexAcquire(s_resp_mutex, portMAX_DELAY);
    s_resp_buf[0] = '\0';
    s_resp_len = 0;
    if (s_resp_mutex) osMutexRelease(s_resp_mutex);
}

bool AT_SendCommand(const char *cmd, uint32_t timeout_ms)
{
    return AT_SendCommandWithResp(cmd, NULL, 0, timeout_ms);
}

bool AT_SendCommandWithResp(const char *cmd, char *resp, uint16_t resp_size, uint32_t timeout_ms)
{
    char cmd_buf[1024];
    bool success = false;
    if (!s_cmd_sem) return false;
    
    // 1. 清空旧信号量（防止上一次超时残留）
    osSemaphoreAcquire(s_cmd_sem, 0);
    
    // 2. 清空全局响应缓冲区
    AT_ClearRespBuffer();
    
    // 3. 发送命令
    snprintf(cmd_buf, sizeof(cmd_buf), "%s\r\n", cmd);
    AT_SendRaw((uint8_t*)cmd_buf, strlen(cmd_buf), 100);
    
    // 4. 等待响应信号量
    if (osSemaphoreAcquire(s_cmd_sem, timeout_ms) == osOK) {
        if (strstr(s_resp_buf, "ERROR") != NULL) {
            success = false;   // 明确失败
        } else {
            success = true;
        }
        if (resp && resp_size) {
            if (s_resp_mutex) osMutexAcquire(s_resp_mutex, portMAX_DELAY);
            strncpy(resp, s_resp_buf, resp_size-1);
            resp[resp_size-1] = '\0';
            if (s_resp_mutex) osMutexRelease(s_resp_mutex);
        }
    } else {
        // 超时后主动清除可能残留的信号量（再次 acquire 0 超时）
        osSemaphoreAcquire(s_cmd_sem, 0);
    }
    return success;
}

bool AT_EnsureGPRSAttach(void)
{
    AT_ClearRespBuffer();
    AT_SendRaw((uint8_t*)"AT+CGATT?\r\n", strlen("AT+CGATT?\r\n"), 100);
    
    uint32_t start = HAL_GetTick();
    bool attached = false;
    while ((HAL_GetTick() - start) < 5000) {
        osDelay(50);
        if (s_resp_mutex) osMutexAcquire(s_resp_mutex, portMAX_DELAY);
        if (strstr(s_resp_buf, "+CGATT: 1") != NULL) attached = true;
        if (strstr(s_resp_buf, "OK") != NULL) break;
        if (s_resp_mutex) osMutexRelease(s_resp_mutex);
    }
    if (s_resp_mutex) osMutexRelease(s_resp_mutex);
    
    if (attached) return true;
    
    if (AT_SendCommand("AT+CGATT=1", 15000)) return true;
    return false;
}

bool AT_SetAPN(void)
{
    char resp[64];
    if (AT_SendCommandWithResp("AT+CSTT=\"\",\"\",\"\"", resp, sizeof(resp), 5000)) {
        if (strstr(resp, "OK") != NULL) return true;
    }
    return false;
}

bool AT_ActivateNetwork(void)
{
    char resp[64];
    if (!AT_SendCommandWithResp("AT+CIICR", resp, sizeof(resp), 30000))
        return false;
    if (strstr(resp, "OK") != NULL) return true;
    return false;
}

bool AT_GetIPAddress(char *ip_buf, uint16_t buf_size)
{
    if (!ip_buf || buf_size == 0) return false;
    
    // 清空全局响应缓冲区
    AT_ClearRespBuffer();
    
    // 发送命令（不等待OK，因为AT+CIFSR不会返回OK）
    AT_SendRaw((uint8_t *)"AT+CIFSR\r\n", strlen("AT+CIFSR\r\n"), 100);
    
    // 轮询响应缓冲区（最多3秒）
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 3000) {
        osDelay(50);
        if (s_resp_mutex) osMutexAcquire(s_resp_mutex, portMAX_DELAY);
        bool has_data = (s_resp_len > 0);
        char temp_buf[64];
        if (has_data) {
            strncpy(temp_buf, s_resp_buf, sizeof(temp_buf) - 1);
            temp_buf[sizeof(temp_buf) - 1] = '\0';
        }
        if (s_resp_mutex) osMutexRelease(s_resp_mutex);
        
        if (has_data) {
            // 查找IP地址（包含点且不含ERROR）
            if (strchr(temp_buf, '.') != NULL && strstr(temp_buf, "ERROR") == NULL) {
                char *ip_start = temp_buf;
                while (*ip_start == ' ' || *ip_start == '\r' || *ip_start == '\n') ip_start++;
                char *ip_end = ip_start;
                while (*ip_end && *ip_end != '\r' && *ip_end != '\n' && *ip_end != ' ') ip_end++;
                size_t len = ip_end - ip_start;
                if (len > 0 && len < buf_size) {
                    memcpy(ip_buf, ip_start, len);
                    ip_buf[len] = '\0';
                    return true;
                }
            }
        }
    }
    return false;
}


bool AT_MQTT_Config(const char *client_id, const char *username, const char *password)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "AT+MCONFIG=\"%s\",\"%s\",\"%s\"", client_id, username, password);
    return AT_SendCommand(cmd, 5000);
}

bool AT_MQTT_Start(const char *server_addr, uint16_t port)
{
    char cmd_buf[256];
    snprintf(cmd_buf, sizeof(cmd_buf), "AT+MIPSTART=\"%s\",%d\r\n", server_addr, port);

    // 清空旧信号量
    osSemaphoreAcquire(s_connect_sem, 0);

    // 直接发送命令
    AT_SendRaw((uint8_t*)cmd_buf, strlen(cmd_buf), 100);

    // 等待 CONNECT OK/FAIL（超时 15 秒）
    if (osSemaphoreAcquire(s_connect_sem, 15000) == osOK) {
        return s_connect_ok;
    }
    return false;
}

bool AT_MQTT_Connect_Wait(uint8_t clean_session, uint16_t keepalive, uint32_t timeout_ms)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+MCONNECT=%d,%d", clean_session, keepalive);
    if (!AT_SendCommand(cmd, 5000)) return false;
    if (osSemaphoreAcquire(s_mqtt_sem, timeout_ms) == osOK) {
        return s_mqtt_conn_ok;
    }
    return false;
}

bool AT_MQTT_Publish(const char *topic, uint8_t qos, uint8_t retain, const char *payload)
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "AT+MPUB=\"%s\",%d,%d,\"%s\"", topic, qos, retain, payload);
    return AT_SendCommand(cmd, 15000);
}

bool AT_MQTT_Subscribe(const char *topic, uint8_t qos)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+MSUB=\"%s\",%d", topic, qos);
    return AT_SendCommand(cmd, 5000);
}

bool AT_MQTT_Disconnect(void)
{
    return AT_SendCommand("AT+MDISCONNECT", 5000);
}

bool AT_MQTT_Close(void)
{
    return AT_SendCommand("AT+MIPCLOSE", 5000);
}

bool AT_CheckRespContains(const char *substr)
{
    bool found = false;
    if (s_resp_mutex) osMutexAcquire(s_resp_mutex, portMAX_DELAY);
    if (strstr(s_resp_buf, substr) != NULL) {
        found = true;
        // 找到后清空缓冲区，避免重复匹配
        s_resp_buf[0] = '\0';
        s_resp_len = 0;
    }
    if (s_resp_mutex) osMutexRelease(s_resp_mutex);
    return found;
}

// UART 接收回调
void AT_Driver_RxCpltCallback_ISR(UART_HandleTypeDef *huart)
{
    static uint8_t rx_byte;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    HAL_UART_Receive_IT(huart, &rx_byte, 1);
    if (rx_byte == '\n' && s_rx_line_idx > 0 && s_rx_line_buf[s_rx_line_idx-1] == '\r') {
        uart_msg_t msg;
        s_rx_line_buf[s_rx_line_idx-1] = '\0';
        msg.len = s_rx_line_idx - 1;
        memcpy(msg.data, s_rx_line_buf, msg.len);
        xQueueSendFromISR(xUartRxQueueHandle, &msg, &xHigherPriorityTaskWoken);
        s_rx_line_idx = 0;
    } else if (s_rx_line_idx < sizeof(s_rx_line_buf)-1) {
        s_rx_line_buf[s_rx_line_idx++] = rx_byte;
    } else {
        s_rx_line_idx = 0;
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}