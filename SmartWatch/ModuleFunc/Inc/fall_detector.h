/*
 * fall_detector.h
 * 跌倒检测算法核心逻辑头文件 (纯算法，无硬件依赖)
 */
#ifndef __FALL_DETECTOR_H__
#define __FALL_DETECTOR_H__

#include <stdint.h>
#include <stdbool.h>
#include "system_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 跌倒检测器上下文结构体
 * 用于保存状态机状态和时间戳，支持多实例
 */
typedef struct {
    uint8_t state;          /* 当前状态: 0=IDLE, 1=FREEFALL, 2=IMPACT, 3=CONFIRMED */
    uint32_t freefall_tick; /* 失重发生的时刻 */
    uint32_t impact_tick;   /* 撞击发生的时刻 */
    bool alarm_triggered;   /* 内部标志：确保单次跌倒事件只触发一次返回 true */
} FallDetector_t;

/**
 * @brief 初始化跌倒检测器
 * @param detector 指向检测器上下文结构的指针
 */
void FallDetector_Init(FallDetector_t* detector);

/**
 * @brief 执行一次跌倒检测逻辑
 * 
 * @param detector 指向检测器上下文结构的指针
 * @param data 指向传感器数据快照的指针 (ax, ay, az...)
 * @param current_tick 当前系统时间戳 (HAL_GetTick())
 * @return 
 *   - true: 刚刚检测到一次新的跌倒事件 (边缘触发，仅第一次确认时返回)
 *   - false: 正常状态，或已处于跌倒确认状态 (不重复触发)
 * 
 * 注意：此函数是纯数学计算，不含任何锁、中断或硬件操作。
 */
bool FallDetector_Process(FallDetector_t* detector, const SensorData_t* data, uint32_t current_tick);

/**
 * @brief (可选) 手动重置检测器
 * 如果需要外部强制复位状态机时调用
 */
void FallDetector_Reset(FallDetector_t* detector);

#ifdef __cplusplus
}
#endif

#endif /* __FALL_DETECTOR_H__ */