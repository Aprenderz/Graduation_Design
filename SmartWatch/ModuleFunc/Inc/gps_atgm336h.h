#ifndef GPS_ATGM336H_H
#define GPS_ATGM336H_H

#include <stdbool.h>
#include "stdint.h"
#include "usart.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 GPS 模块（调试打印等）
 * @return true 始终返回 true（或可扩展为检测结果）
 */
bool GPS_ATGM336H_Init(void);

/**
 * @brief 轮询环形缓冲区，组装 NMEA 句子并解析
 * @note  该函数应定期调用（例如在 SensorTask 中每 50ms 调用一次）
 */
void GPS_ATGM336H_Poll(void);

/**
 * @brief 获取最新一次解析的 GPS 数据（无锁，仅从静态变量读取）
 * @param lat  输出纬度
 * @param lon  输出经度
 * @param valid 输出有效标志（true 表示定位有效）
 */
void GPS_ATGM336H_GetLatest(float *lat, float *lon, bool *valid);

/**
 * @brief 供 UART 中断调用的函数，将接收到的字节放入环形缓冲区
 * @param ch 接收到的字节
 */
void GPS_ATGM336H_RxChar(uint8_t ch);

/**
 * @brief 供 UART 中断调用的函数，将环形缓冲区中的数据发送给串口
 * @param huart 串口句柄
 */
void GPS_ATGM336H_RxCpltCallback_ISR(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif