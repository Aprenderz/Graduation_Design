/*
 * ds18b20.h
 *
 * DS18B20 Temperature Sensor Driver for STM32 HAL (1-Wire Protocol)
 * Pin: PB5 (GPIO_Output, Open-Drain, Pull-up)
 */

#ifndef __DS18B20_H__
#define __DS18B20_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

// 定义 DS18B20 连接的 GPIO 端口和引脚
// 请根据 CubeMX 实际配置确认，此处默认为 PB5
#define DS18B20_PORT    GPIOB
#define DS18B20_PIN     GPIO_PIN_5

/**
 * @brief 初始化 DS18B20 传感器
 * @return true 如果检测到设备，false 如果未检测到
 */
bool DS18B20_Init(void);

/**
 * @brief 读取温度值
 * @param temperature 指针用于存储读取到的温度值 (摄氏度)
 * @return true 如果读取成功，false 如果失败
 */
bool DS18B20_ReadTemp(float* temperature);

/** 启动一次温度转换（非阻塞） */
void DS18B20_StartConversion(void);

#ifdef __cplusplus
}
#endif

#endif // __DS18B20_H__