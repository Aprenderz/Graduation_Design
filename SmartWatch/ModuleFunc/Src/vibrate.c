#include "vibrate.h"

#include "tim.h"
#include "main.h"

void VIBRA_Start(uint8_t duty_percent, uint32_t duration_ms)
{
    if (duty_percent > 100u) duty_percent = 100u;

    // duty_percent 映射到 PWM 周期
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim3);
    uint32_t pulse = (period * duty_percent) / 100u;

    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, pulse);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);

    if (duration_ms > 0)
    {
        osDelay(duration_ms);
        VIBRA_Stop();
    }
}

void VIBRA_Stop(void)
{
    // 直接关闭 PWM
    HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_3);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);
}

