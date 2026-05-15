/*
 * mpu6050.h
 */
#ifndef __MPU6050_H__
#define __MPU6050_H__

#include <stdbool.h>

#include "i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 */
bool MPU6050_Init(I2C_HandleTypeDef* hi2c);

/**
 * @brief 仅从硬件读取原始字节到缓冲区
 * 
 * @param hi2c I2C句柄
 * @param buf 指向至少14字节缓冲区的指针
 * @return true: 通信成功, false: 通信失败
 * 
 * 注意：此函数内部会获取并释放 I2C 互斥锁。
 */
bool MPU6050_ReadRawBytes(I2C_HandleTypeDef* hi2c, uint8_t* buf);

/**
 * @brief 将原始字节解析并直接填入系统全局结构体
 * 
 * @param raw_buf 指向包含14字节原始数据的缓冲区
 * @param pSysData 指向全局 g_SensorData 结构体的指针
 * 
 * 注意：
 * 1. 此函数【不】包含任何锁操作。
 * 2. 此函数【不】进行任何 I2C 通信。
 * 3. 调用者必须确保在持有 xDataMutexHandle 的情况下调用此函数，以保证写入原子性。
 */
void MPU6050_ParseAndFill_SystemData(const uint8_t* raw_buf, SensorData_t* pSysData);

#ifdef __cplusplus
}
#endif

#endif /* __MPU6050_H__ */