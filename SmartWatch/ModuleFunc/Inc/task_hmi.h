/*
 * task_hmi.h
 * HMI 任务接口：OLED 显示、按键扫描
 */

#ifndef __TASK_HMI_H__
#define __TASK_HMI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief HMI 任务入口函数
 * @param argument: FreeRTOS 传递的参数 (本例未使用)
 * @note 此函数负责刷新屏幕显示和处理用户输入
 */
void HMITask(void *argument);

/**
 * @brief (可选) 强制立即刷新屏幕
 * @note 通常在状态发生剧烈变化时调用，打破正常的延时循环
 */
void HMI_ForceRefresh(void);

#ifdef __cplusplus
}
#endif

#endif /* __TASK_HMI_H__ */