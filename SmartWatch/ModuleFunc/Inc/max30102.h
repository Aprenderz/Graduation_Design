#ifndef __MAX30102_H
#define __MAX30102_H

#include "stm32f4xx_hal.h"
#include "stdbool.h"

// ========== 算法参数 ==========
#define FS              100
#define BUFFER_SIZE     (FS * 5)       // 500 样本，5 秒数据
#define MA4_SIZE        4
#define HAMMING_SIZE    5

// 寄存器地址
#define REG_INTR_STATUS_1   0x00
#define REG_INTR_STATUS_2   0x01
#define REG_INTR_ENABLE_1   0x02
#define REG_INTR_ENABLE_2   0x03
#define REG_FIFO_WR_PTR     0x04
#define REG_OVF_COUNTER     0x05
#define REG_FIFO_RD_PTR     0x06
#define REG_FIFO_DATA       0x07
#define REG_FIFO_CONFIG     0x08
#define REG_MODE_CONFIG     0x09
#define REG_SPO2_CONFIG     0x0A
#define REG_LED1_PA         0x0C
#define REG_LED2_PA         0x0D
#define REG_PILOT_PA        0x10
#define REG_MULTI_LED_CTRL1 0x11
#define REG_MULTI_LED_CTRL2 0x12
#define REG_TEMP_INTR       0x1F
#define REG_TEMP_FRAC       0x20
#define REG_TEMP_CONFIG     0x21
#define REG_REV_ID          0xFE
#define REG_PART_ID         0xFF

// MAX30102 设备地址（7位地址0x57，左移一位为0xAE）
#define MAX30102_I2C_ADDR   0xAE

// ========== 公开接口 ==========
bool MAX30102_Init(I2C_HandleTypeDef *hi2c);
void MAX30102_PushSample(I2C_HandleTypeDef *hi2c);
void MAX30102_GetProcessedData(uint8_t *hr, uint8_t *spo2, bool *valid);
bool IsFingerPresent(void);

#endif