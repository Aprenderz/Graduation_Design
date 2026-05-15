#ifndef __AT_DRIVER_H__
#define __AT_DRIVER_H__

#include <stdint.h>
#include <stdbool.h>
#include "usart.h"

#ifdef __cplusplus
extern "C" {
#endif

// UART 接收消息结构（用于队列）
typedef struct {
    char data[512];
    uint16_t len;
} uart_msg_t;

// AT 驱动初始化
void AT_Driver_Init(void);

// 线程安全的原始数据发送（阻塞，带超时）
bool AT_SendRaw(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

// 适应波特率
void AT(void);

// 发送 AT 命令（不关心响应内容）
bool AT_SendCommand(const char *cmd, uint32_t timeout_ms);

// 发送 AT 命令并获取响应内容
bool AT_SendCommandWithResp(const char *cmd, char *resp, uint16_t resp_size, uint32_t timeout_ms);

// 注册 URC 回调
void AT_RegisterURC(const char *prefix, void (*callback)(const char *line));

// GPRS 附着检查与附着
bool AT_EnsureGPRSAttach(void);

// 设置 APN（使用空参数，自动获取）
bool AT_SetAPN(void);

// 激活移动网络
bool AT_ActivateNetwork(void);

// 获取本地 IP 地址（AT+CIFSR）
bool AT_GetIPAddress(char *ip_buf, uint16_t buf_size);

// MQTT 相关命令
bool AT_MQTT_Config(const char *client_id, const char *username, const char *password);
bool AT_MQTT_Start(const char *server_addr, uint16_t port);
bool AT_MQTT_Connect_Wait(uint8_t clean_session, uint16_t keepalive, uint32_t timeout_ms);
bool AT_MQTT_Publish(const char *topic, uint8_t qos, uint8_t retain, const char *payload);
bool AT_MQTT_Subscribe(const char *topic, uint8_t qos);
bool AT_MQTT_Disconnect(void);
bool AT_MQTT_Close(void);

// 清空全局响应缓冲区（用于短信模块等）
void AT_ClearRespBuffer(void);
bool AT_CheckRespContains(const char *substr);  // 检查响应缓冲区是否包含子串，找到后可选清空

// UART 接收完成回调
void AT_Driver_RxCpltCallback_ISR(UART_HandleTypeDef *huart);

// URC 处理任务（需在 FreeRTOS 中创建）
void URC_Task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* __AT_DRIVER_H__ */