/*
 * task_sensor.h
 * 传感器任务头文件
 * 
 * 功能描述:
 * 1. 声明传感器采集任务入口函数 SensorTask
 * 2. 声明独立的传感器数据读取接口，供其他模块按需调用（可选）
 * 3. 包含必要的系统头文件和全局数据结构引用
 */

#ifndef __TASK_SENSOR_H__
#define __TASK_SENSOR_H__

#ifdef __cplusplus
extern "C" {
#endif

/* 包含必要的系统定义和数据类型 */
#include "main.h"
 /* 引入 g_SensorData 全局变量定义 */

/**
 * @brief 传感器任务入口函数
 * @param argument 任务参数 (未使用)
 * 
 * 该函数由 FreeRTOS 创建并调度，负责周期性采集所有传感器数据
 * 并更新到全局结构体 g_SensorData 中。
 */
void SensorTask(void *argument);

/**
 * @brief 独立读取 DS18B20 温度数据并同步到全局变量
 * @return true: 读取成功且数据有效, false: 读取失败或传感器未初始化
 * 
 * 注意：该函数内部会获取互斥锁，请勿在中断上下文或已持有相关锁的任务中调用。
 * 通常仅在 SensorTask 内部调用，若需外部强制刷新可开放此接口。
 */
// void Read_DS18B20(void);

/**
 * @brief 独立读取 MPU6050 六轴数据并同步到全局变量
 * @return true: 读取成功且数据有效, false: 读取失败或传感器未初始化
 * 
 * 注意：该函数内部会获取互斥锁，请勿在中断上下文或已持有相关锁的任务中调用。
 * 实现了 I2C 锁与数据锁的分离，确保线程安全。
 */
// void Read_MPU6050(void);

#ifdef __cplusplus
}
#endif

#endif /* __TASK_SENSOR_H__ */