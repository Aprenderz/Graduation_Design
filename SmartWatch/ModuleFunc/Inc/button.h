/*
 * button.h
 * 按键驱动头文件
 * - SOS按键：PB12，支持短按连按5次触发SOS、长按10秒触发SOS
 * - 功能按键：PB14，点击切换OLED显示模式（正常显示 / 地址显示）
 */

#ifndef __BUTTON_H__
#define __BUTTON_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Button_Init(void);
void Button_Scan(void);   // 需在HMI任务中周期性调用（建议10~20ms周期）

#ifdef __cplusplus
}
#endif

#endif /* __BUTTON_H__ */