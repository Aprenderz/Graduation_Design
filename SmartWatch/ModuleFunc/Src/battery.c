#include "battery.h"
#include "adc.h"
#include "main.h"

extern ADC_HandleTypeDef hadc1;

// 电阻分压系数：Battery_V = ADC_V * BATTERY_VDIV_K
// 需要你根据实际分压电阻 R1/R2 校准：K = (R1+R2)/R2
#ifndef BATTERY_VDIV_K
#define BATTERY_VDIV_K (2.0f)
#endif

#ifndef BATTERY_ADC_REF_V
#define BATTERY_ADC_REF_V (3.3f)
#endif

BatteryData_t Battery_Read(void)
{
    BatteryData_t out = {0};

    // 单次采样
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 50) != HAL_OK)
    {
        HAL_ADC_Stop(&hadc1);
        out.valid = false;
        return out;
    }

    uint32_t raw = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    // 12bit
    float adc_v = ((float)raw * BATTERY_ADC_REF_V) / 4095.0f;
    out.voltage = adc_v * BATTERY_VDIV_K;
    out.valid = true;
    return out;
}

