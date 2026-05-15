#include "task_max30102.h"
#include "max30102.h"
#include "main.h"
#include "i2c.h"

static bool s_max30102_ok = false;
static bool last_finger_present = false;
static TickType_t finger_gone_start = 0;

void MAX30102_Task(void *argument)
{
    // 初始化传感器
    s_max30102_ok = MAX30102_Init(&hi2c3);
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10);
    uint32_t last_update_ms = 0;
    uint8_t last_valid_hr = 0, last_valid_spo2 = 0;
    bool has_valid_once = false;  // 是否曾获得过有效数据
    for (;;) {
        if (s_max30102_ok) {
            MAX30102_PushSample(&hi2c3);
            bool finger = IsFingerPresent();
            
            // 手指状态变化检测
            if (!last_finger_present && finger) {
                // 手指刚放上，重置计时
                finger_gone_start = 0;
            } else if (last_finger_present && !finger) {
                // 手指刚移开，记录开始时间
                finger_gone_start = xTaskGetTickCount();
            }
            last_finger_present = finger;
            uint32_t now = xTaskGetTickCount();
            if (now - last_update_ms >= pdMS_TO_TICKS(500)) {
                uint8_t hr = 0, spo2 = 0;
                bool valid = false;
                MAX30102_GetProcessedData(&hr, &spo2, &valid);
                HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

                if (valid && finger) {
                    last_valid_hr = hr;
                    last_valid_spo2 = spo2;
                    has_valid_once = true;
                }
                
                // 更新全局变量
                if (osMutexAcquire(xDataMutexHandle, portMAX_DELAY) == osOK) {
                    // 判断是否应该清空显示：手指移开超过 1 秒
                    bool should_clear = (finger_gone_start != 0) && 
                                        (now - finger_gone_start > pdMS_TO_TICKS(1000));
                    
                    if (should_clear) {
                        g_SensorData.hr_valid = false;
                        g_SensorData.heart_rate = 0;
                        g_SensorData.spo2 = 0;
                    } else if (finger && has_valid_once) {
                        // 手指存在且至少有过一次有效数据，显示上次有效值（即使当前 valid=false）
                        g_SensorData.heart_rate = last_valid_hr;
                        g_SensorData.spo2 = last_valid_spo2;
                        g_SensorData.hr_valid = true;
                        g_SensorData.last_update_tick = now;
                    } else if (!finger && !should_clear) {
                        // 手指刚移开但未超时，保留上次显示（不清空）
                        // 什么都不做，保留原有值
                    }
                    osMutexRelease(xDataMutexHandle);
                }
                last_update_ms = now;
            }
        }
        vTaskDelayUntil(&last_wake, period);
    }
}