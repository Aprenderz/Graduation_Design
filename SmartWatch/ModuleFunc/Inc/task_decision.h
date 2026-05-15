/*
 * task_decision.h
 * 决策任务接口：跌倒检测、报警逻辑
 */

#ifndef __TASK_DECISION_H__
#define __TASK_DECISION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "main.h"

/**
 * @brief 触发系统报警
 * 
 * @param message 报警消息 (如 "体温过高")
 * @param state 系统状态 (如 SYS_STATE_WARNING)
 * @param reason 报警原因 (如 "体温超过阈值")
 */
void Decision_TriggerAlert(const char *message, SystemState_t state,const char *reason);

/**
 * @brief 决策任务入口函数
 * 
 * 该任务负责：
 * 1. 周期性读取传感器数据
 * 2. 运行跌倒检测算法 (fall_detector)
 * 3. 根据算法结果更新全局系统状态 (g_SystemStatus)
 *    - 检测到跌倒: 设置为 SYS_STATE_FALL_ALARM
 *    - 恢复平稳: 重置为 SYS_STATE_NORMAL
 * 4. 触发报警流程 (预留接口)
 * 
 * @param argument 任务参数 (通常为 NULL)
 */
void DecisionTask(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* __TASK_DECISION_H__ */