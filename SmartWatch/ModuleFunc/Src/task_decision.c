/**
 * @file task_decision.c
 * @brief 决策任务实现：生理参数异常检测、久坐提醒、报警状态更新
 */

#include "task_decision.h"
#include "main.h"
#include "usart.h"
#include "math.h"
#include "tw_tts.h"
#include "vibrate.h"
#include <string.h>
#include <stdio.h>

/* ----------------------------- 内部数据结构 ----------------------------- */
typedef struct {
    bool temp_alerted;
    bool hr_low_alerted;
    bool hr_high_alerted;
    bool spo2_low_alerted;
    bool sedentary_alerted;
} AlertState_t;

static AlertState_t s_alert_state = {false};

/* ----------------------------- 久坐检测宏与私有变量 ----------------------------- */
#define STILL_THRESHOLD         0.15f       // 合加速度与 1g 的差值小于此值视为静止
#define SEDENTARY_TIMEOUT_MS    60000     // 静止 30 分钟触发提醒 (1800000 ms)

static bool     s_is_still = false;
static uint32_t s_still_start = 0;

/* ----------------------------- 辅助函数声明 ----------------------------- */
static void Decision_GetDataSnapshot(SensorData_t *sensor);
static void Decision_CheckTemperature(const SensorData_t *sensor);
static void Decision_CheckHeartRate(const SensorData_t *sensor);
static void Decision_CheckSpO2(const SensorData_t *sensor);
static void Decision_CheckSedentary(const SensorData_t *sensor, uint32_t now);

/* ----------------------------- 公共接口实现 ----------------------------- */
void DecisionTask(void *argument)
{
    TTS_Init(&huart6);
    SensorData_t sensor_snap;

    for (;;)
    {
        Decision_GetDataSnapshot(&sensor_snap);

        Decision_CheckTemperature(&sensor_snap);
        Decision_CheckHeartRate(&sensor_snap);
        Decision_CheckSpO2(&sensor_snap);
        Decision_CheckSedentary(&sensor_snap, HAL_GetTick());

        osDelay(500);
    }
}

/* ----------------------------- 获取数据快照 ----------------------------- */
static void Decision_GetDataSnapshot(SensorData_t *sensor)
{
    if (osMutexAcquire(xDataMutexHandle, osWaitForever) == osOK)
    {
        if (sensor != NULL) {
            *sensor = g_SensorData;
        }
        osMutexRelease(xDataMutexHandle);
    }
}

/* ----------------------------- 体温检测 ----------------------------- */
static void Decision_CheckTemperature(const SensorData_t *sensor)
{
    if (!sensor->temp_valid)
        return;

    bool should_alert = false;
    if (sensor->temperature > 36.4f && !s_alert_state.temp_alerted) {
        should_alert = true;
        s_alert_state.temp_alerted = true;
    } else if (sensor->temperature <= 36.4f && s_alert_state.temp_alerted) {
        s_alert_state.temp_alerted = false;
    }

    if (should_alert) {
        Decision_TriggerAlert("体温过高", SYS_STATE_WARNING, "Abnormal_temp");
    }
}

/* ----------------------------- 心率检测 ----------------------------- */
static void Decision_CheckHeartRate(const SensorData_t *sensor)
{
    if (!sensor->hr_valid)
        return;

    uint8_t hr = sensor->heart_rate;
    bool should_alert_low = false;
    bool should_alert_high = false;

    if (hr < 50 && !s_alert_state.hr_low_alerted) {
        should_alert_low = true;
        s_alert_state.hr_low_alerted = true;
    } else if (hr >= 50 && s_alert_state.hr_low_alerted) {
        s_alert_state.hr_low_alerted = false;
    }

    if (hr > 120 && !s_alert_state.hr_high_alerted) {
        should_alert_high = true;
        s_alert_state.hr_high_alerted = true;
    } else if (hr <= 120 && s_alert_state.hr_high_alerted) {
        s_alert_state.hr_high_alerted = false;
    }

    if (should_alert_low) {
        Decision_TriggerAlert("心率过低", SYS_STATE_WARNING, "Low_heart_rate");
    }
    if (should_alert_high) {
        Decision_TriggerAlert("心率过高", SYS_STATE_WARNING, "High_heart_rate");
    }
}

/* ----------------------------- 血氧检测 ----------------------------- */
static void Decision_CheckSpO2(const SensorData_t *sensor)
{
    if (!sensor->hr_valid)
        return;

    uint8_t spo2 = sensor->spo2;
    bool should_alert = false;

    if (spo2 < 90 && !s_alert_state.spo2_low_alerted) {
        should_alert = true;
        s_alert_state.spo2_low_alerted = true;
    } else if (spo2 >= 90 && s_alert_state.spo2_low_alerted) {
        s_alert_state.spo2_low_alerted = false;
    }

    if (should_alert) {
        Decision_TriggerAlert("血氧过低", SYS_STATE_WARNING, "Low_spo2");
    }
}

/* ----------------------------- 久坐检测 ----------------------------- */
static void Decision_CheckSedentary(const SensorData_t *sensor, uint32_t now)
{
    if (!sensor->mpu_valid)
        return;

    // ========== 读取久坐提醒开关 ==========
    bool sed_enabled = false;
    if (osMutexAcquire(xDataMutexHandle, 100) == osOK) {
        sed_enabled = g_SystemStatus.sedentary_enabled;
        osMutexRelease(xDataMutexHandle);
    } else {
        return;   // 拿不到锁则安全跳过本次检测
    }

    // 关闭状态：强制重置静止计时（防止误判）
    if (!sed_enabled) {
        if (s_is_still) {
            s_is_still = false;
            s_still_start = 0;
            s_alert_state.sedentary_alerted = false;
        }
        return;
    }

    float mag = sqrtf(sensor->ax * sensor->ax + sensor->ay * sensor->ay + sensor->az * sensor->az);
    bool now_still = (fabsf(mag - 1.0f) < STILL_THRESHOLD);

    if (now_still && !s_is_still) {
        s_is_still = true;
        s_still_start = now;
        s_alert_state.sedentary_alerted = false;   // 重置提醒标志
    } else if (!now_still && s_is_still) {
        s_is_still = false;
        s_still_start = 0;
        s_alert_state.sedentary_alerted = false;
    }

    if (s_is_still && !s_alert_state.sedentary_alerted &&
        (now - s_still_start >= SEDENTARY_TIMEOUT_MS))
    {
        s_alert_state.sedentary_alerted = true;
        Decision_TriggerAlert("久坐提醒", SYS_STATE_WARNING, "Sedentary");
    }
}

/* ----------------------------- 报警触发函数 ----------------------------- */
void Decision_TriggerAlert(const char *message, SystemState_t state, const char *reason)
{
    // ========== 相同报警冷却 1 分钟 ==========
    static char s_last_reason_cooling[18] = {0};
    static uint32_t s_last_reason_tick = 0;
    uint32_t now = HAL_GetTick();

    // 如果 reason 有效，且与上次冷却中的原因相同，且时间差小于 60 秒，则直接退出
    if (reason != NULL && s_last_reason_cooling[0] != '\0' &&
        strcmp(reason, s_last_reason_cooling) == 0 && (now - s_last_reason_tick < 60000))
    {
        return;
    }

    // 更新冷却记录
    if (reason != NULL) {
        strncpy(s_last_reason_cooling, reason, sizeof(s_last_reason_cooling) - 1);
        s_last_reason_cooling[sizeof(s_last_reason_cooling) - 1] = '\0';
    } else {
        s_last_reason_cooling[0] = '\0';
    }
    s_last_reason_tick = now;
    
    if (osMutexAcquire(xDataMutexHandle, osWaitForever) == osOK)
    {
        // 优先级规则：SOS > 跌倒 > Warning
        bool should_update = false;
        if (g_SystemStatus.state == SYS_STATE_NORMAL) {
            should_update = true;
        } else if (g_SystemStatus.state == SYS_STATE_WARNING) {
            // Warning 可被 SOS 或跌倒覆盖
            should_update = (state == SYS_STATE_FALL_ALARM || state == SYS_STATE_SOS_ALARM);
        } else if (g_SystemStatus.state == SYS_STATE_FALL_ALARM) {
            // 跌倒只可被 SOS 覆盖
            should_update = (state == SYS_STATE_SOS_ALARM);
        } else if (g_SystemStatus.state == SYS_STATE_SOS_ALARM) {
            // SOS 不可被任何其他状态覆盖
            should_update = false;
        }

        if (should_update)
        {
            g_SystemStatus.state = state;
            g_SystemStatus.alarm_timestamp = HAL_GetTick();
            if (reason != NULL && strcmp(reason, "Sedentary") != 0)
                g_SystemStatus.need_upload = true;
            strncpy(g_SystemStatus.alert_message, message, sizeof(g_SystemStatus.alert_message) - 1);
            g_SystemStatus.alert_message[sizeof(g_SystemStatus.alert_message) - 1] = '\0';

            // 报警原因
            if (reason != NULL) {
                strncpy(g_SystemStatus.alarm_reason, reason, sizeof(g_SystemStatus.alarm_reason) - 1);
                g_SystemStatus.alarm_reason[sizeof(g_SystemStatus.alarm_reason) - 1] = '\0';
            } else {
                g_SystemStatus.alarm_reason[0] = '\0';
            }

            g_SystemStatus.popup_pending = true;    // 通知 HMI 有新弹窗
        }
        osMutexRelease(xDataMutexHandle);
    }

    // 本地告警动作（TTS 播报、振动）
    if (state != SYS_STATE_SOS_ALARM) {
        TTS_Play(message, TTS_ENCODE_UTF8);
        VIBRA_Start(80, 5000);
    } else {
        VIBRA_Start(80, 1000);   // 电话流程短震
    }
}