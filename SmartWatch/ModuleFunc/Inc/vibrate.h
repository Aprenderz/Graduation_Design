#ifndef __VIBRATE_H__
#define __VIBRATE_H__

#include <stdint.h>
#include <stdbool.h>

// 震动（PWM 占空比 0~100，持续毫秒）
void VIBRA_Start(uint8_t duty_percent, uint32_t duration_ms);

// 立刻停止震动
void VIBRA_Stop(void);

#endif /* __VIBRATE_H__ */

