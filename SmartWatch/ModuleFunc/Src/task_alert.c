#include "task_alert.h"
#include "task_decision.h"
#include "fall_detector.h"
#include "phone.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

static FallDetector_t s_fallDetector;

void AlertTask(void *argument)
{
    FallDetector_Init(&s_fallDetector);

    const TickType_t period = pdMS_TO_TICKS(50);
    TickType_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        SensorData_t sensor_snap;
        HMIInput_t   hmi_snap;
        bool fall_triggered = false;
        bool sos_triggered  = false;

        /* 1. 加锁获取传感器和 HMI 输入 */
        if (osMutexAcquire(xDataMutexHandle, portMAX_DELAY) == osOK)
        {
            sensor_snap = g_SensorData;
            hmi_snap    = g_HMIInput;

            // 只在非跌倒报警状态下检查 SOS 触发条件
            if (g_SystemStatus.state != SYS_STATE_FALL_ALARM) {
                if (hmi_snap.sos_short_press_count >= 5) {
                    sos_triggered = true;
                    g_HMIInput.sos_short_press_count = 0;   // 清零连按计数
                }
            }

            osMutexRelease(xDataMutexHandle);
        }
        else
        {
            vTaskDelayUntil(&last_wake, period);
            continue;
        }

        /* 2. 跌倒检测 */
        if (!sos_triggered && sensor_snap.mpu_valid)
        {
            uint32_t now = HAL_GetTick();
            if (FallDetector_Process(&s_fallDetector, &sensor_snap, now))
                fall_triggered = true;
        }

        /* 3. 处理 SOS 报警（连按5次触发） */
        if (sos_triggered)
        {
            Decision_TriggerAlert("SOS...", SYS_STATE_SOS_ALARM, "SOS");

            char tts_text[512];
            snprintf(tts_text, sizeof(tts_text),
                     "SOS求救！老人位于北纬%.6f度，东经%.6f度，心率%d次每分，血氧%d%%，体温%.1f度，请立即援助。",
                     sensor_snap.latitude, sensor_snap.longitude,
                     sensor_snap.heart_rate, sensor_snap.spo2, sensor_snap.temperature);

            Decision_ExecuteCall(tts_text, 18000);
        }
        /* 4. 处理跌倒报警（带取消等待） */
        else if (fall_triggered)
        {
            Decision_TriggerAlert("监测到跌倒", SYS_STATE_FALL_ALARM, "Fall");

            // 10 秒等待取消
            const TickType_t cancel_timeout = pdMS_TO_TICKS(10000);
            TickType_t start_wait = xTaskGetTickCount();
            bool cancelled = false;

            while ((xTaskGetTickCount() - start_wait) < cancel_timeout)
            {
                // 仅检查统一的取消标志（由 HMI 任务设置）
                if (osMutexAcquire(xDataMutexHandle, pdMS_TO_TICKS(20)) == osOK)
                {
                    cancelled = g_SystemStatus.fall_cancelled;
                    osMutexRelease(xDataMutexHandle);
                }
                if (cancelled)
                    break;

                vTaskDelay(pdMS_TO_TICKS(50));
            }

            if (!cancelled)
            {
                // 倒计时结束，执行电话呼叫
                char tts_text[512];
                snprintf(tts_text, sizeof(tts_text),
                         "跌倒求救！老人位于北纬%.6f度，东经%.6f度，心率%d次每分，血氧%d%%，体温%.1f度。请立即援助。",
                         sensor_snap.latitude, sensor_snap.longitude,
                         sensor_snap.heart_rate, sensor_snap.spo2, sensor_snap.temperature);
                Decision_ExecuteCall(tts_text, 17000);
            }

            // 清除取消标志，恢复状态（不影响 HMI 显示“已取消”弹窗）
            if (osMutexAcquire(xDataMutexHandle, portMAX_DELAY) == osOK)
            {
                g_SystemStatus.fall_cancelled = false;
                
                if (cancelled) {
                    // 取消成功：恢复系统状态为正常
                    g_SystemStatus.state = SYS_STATE_NORMAL;
                    g_SystemStatus.alarm_reason[0] = '\0';
                }
                // 注意：不再操作 g_HMIInput.sos_long_press 或 sos_short_press_count
                osMutexRelease(xDataMutexHandle);
                FallDetector_Reset(&s_fallDetector);
            }
        }

        vTaskDelayUntil(&last_wake, period);
    }
}