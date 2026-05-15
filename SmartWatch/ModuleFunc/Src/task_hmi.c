#include "task_hmi.h"
#include "main.h"
#include "oled_sh1106.h"
#include "button.h"
#include "phone.h"
#include "i2c.h"
#include <stdio.h>
#include <string.h>

typedef enum {
    DISPLAY_MODE_NORMAL,
    DISPLAY_MODE_ADDRESS,
    DISPLAY_MODE_ALERT,
    DISPLAY_MODE_CANCEL_MSG
} DisplayMode_t;

static DisplayMode_t s_display_mode = DISPLAY_MODE_NORMAL;

#define ALERT_POPUP_MS      5000
#define FALL_ALERT_MS       10000
#define CANCEL_MSG_MS       2000

static uint32_t s_alert_start_tick = 0;
static char s_alert_line1[32] = {0};
static char s_alert_line2[32] = {0};
static char s_alert_line3[32] = {0};

// 本地状态，不依赖全局 alarm_reason
static char s_current_alert_reason[18] = {0};   // 当前弹窗原因
static bool s_force_address = false;            // 地址页面锁定

static char s_info_msg[32] = {0};
static int  s_info_col = 38;

static void HMI_DrawPopup(const char *msg, int col)
{
    SH1106_Clear();
    SH1106_DrawUTF8String(3, col, msg);
    SH1106_Update();
}

static void HMI_DisplayLoop(void)
{
    char line1[32], line2[32], line3[32], line4[32];
    SensorData_t sensor_snap;
    SystemStatus_t status_snap;
    HMIInput_t hmi_snap;
    FamilyConfig_t family_snap;

    if (osMutexAcquire(xDataMutexHandle, 100) == osOK) {
        sensor_snap = g_SensorData;
        status_snap = g_SystemStatus;
        hmi_snap    = g_HMIInput;
        family_snap = g_FamilyConfig;
        osMutexRelease(xDataMutexHandle);
    } else {
        return;
    }

    uint32_t now = HAL_GetTick();

    /* ---------- 1. 提示消息模式 ---------- */
    if (s_display_mode == DISPLAY_MODE_CANCEL_MSG) {
        if (now - s_alert_start_tick >= CANCEL_MSG_MS) {
            s_display_mode = DISPLAY_MODE_NORMAL;
            s_current_alert_reason[0] = '\0';
        } else {
            HMI_DrawPopup(s_info_msg, s_info_col);
            return;
        }
    }

    /* ---------- 2. 弹窗触发（使用全局 popup_pending 和本地 reason 快照） ---------- */
    if (status_snap.popup_pending && status_snap.alarm_reason[0] != '\0')
    {
        const char *reason = status_snap.alarm_reason;

        // 如果已在展示同一个弹窗，不重复初始化
        bool same_alert_active = (s_display_mode == DISPLAY_MODE_ALERT &&
                                  strcmp(reason, s_current_alert_reason) == 0);

        if (!same_alert_active)
        {
            // 保存本地原因副本
            strncpy(s_current_alert_reason, reason, sizeof(s_current_alert_reason)-1);
            s_current_alert_reason[sizeof(s_current_alert_reason)-1] = '\0';

            // 准备固定文本
            strncpy(s_alert_line1, status_snap.alert_message, sizeof(s_alert_line1)-1);
            s_alert_line1[sizeof(s_alert_line1)-1] = '\0';

            if (strcmp(s_current_alert_reason, "Fall") == 0) {
                strcpy(s_alert_line2, "即将拨打电话");
                strcpy(s_alert_line3, "长按SOS键取消");
                s_force_address = false;  // 新跌倒重置锁定
            } else if (strcmp(s_current_alert_reason, "Sedentary") == 0) {
                strcpy(s_alert_line2, "请适当活动");
                s_alert_line3[0] = '\0';
            } else {
                s_alert_line2[0] = '\0';
                if (strcmp(s_current_alert_reason, "Abnormal_temp") == 0)
                    snprintf(s_alert_line2, sizeof(s_alert_line2), ">37.3 ℃");
                else if (strcmp(s_current_alert_reason, "High_heart_rate") == 0)
                    snprintf(s_alert_line2, sizeof(s_alert_line2), ">120 次/分");
                else if (strcmp(s_current_alert_reason, "Low_heart_rate") == 0)
                    snprintf(s_alert_line2, sizeof(s_alert_line2), "<50 次/分");
                else if (strcmp(s_current_alert_reason, "Low_spo2") == 0)
                    snprintf(s_alert_line2, sizeof(s_alert_line2), "<90 %%");
                s_alert_line3[0] = '\0';
            }

            s_display_mode = DISPLAY_MODE_ALERT;
            s_alert_start_tick = now;
        }
    }

    /* ---------- 3. 按键处理 ---------- */
    // 长按 SOS 取消跌倒
    if (hmi_snap.sos_long_press)
    {
        bool is_fall_alert = (s_display_mode == DISPLAY_MODE_ALERT && 
                              strcmp(s_current_alert_reason, "Fall") == 0);
        if (osMutexAcquire(xDataMutexHandle, 100) == osOK) {
            if (is_fall_alert) {
                g_SystemStatus.fall_cancelled = true;
                g_SystemStatus.popup_pending = false;
            }
            g_HMIInput.sos_long_press = false;
            osMutexRelease(xDataMutexHandle);
        }
        if (is_fall_alert) {
            strcpy(s_info_msg, "已取消");
            s_info_col = 38;
            s_display_mode = DISPLAY_MODE_CANCEL_MSG;
            s_alert_start_tick = now;
            s_current_alert_reason[0] = '\0';
            s_force_address = false;
            HMI_DrawPopup(s_info_msg, s_info_col);
            return;
        }
    }

    // 功能键长按：切换久坐开关
    if (hmi_snap.fun_long_press)
    {
        if (osMutexAcquire(xDataMutexHandle, 100) == osOK) {
            bool new_state = !g_SystemStatus.sedentary_enabled;
            g_SystemStatus.sedentary_enabled = new_state;
            if (!new_state && 
                g_SystemStatus.state == SYS_STATE_WARNING &&
                strcmp(g_SystemStatus.alarm_reason, "Sedentary") == 0) {
                g_SystemStatus.state = SYS_STATE_NORMAL;
                g_SystemStatus.alarm_reason[0] = '\0';
                g_SystemStatus.popup_pending = false;
            }
            g_HMIInput.fun_long_press = false;
            osMutexRelease(xDataMutexHandle);
        }
        strcpy(s_info_msg, g_SystemStatus.sedentary_enabled ? "久坐已开启" : "久坐已关闭");
        s_info_col = 28;
        s_display_mode = DISPLAY_MODE_CANCEL_MSG;
        s_alert_start_tick = now;
        s_current_alert_reason[0] = '\0';
        HMI_DrawPopup(s_info_msg, s_info_col);
        return;
    }

    // 功能键短按切换显示
    if (hmi_snap.oled_switch_press) {
        if (osMutexAcquire(xDataMutexHandle, 100) == osOK) {
            g_HMIInput.oled_switch_press = false;
            osMutexRelease(xDataMutexHandle);
        }

        if (s_display_mode == DISPLAY_MODE_ALERT) {
            // 手动关闭弹窗（本地处理）
            if (osMutexAcquire(xDataMutexHandle, 100) == osOK) {
                g_SystemStatus.popup_pending = false;
                osMutexRelease(xDataMutexHandle);
            }

            if (strcmp(s_current_alert_reason, "Fall") == 0 && !status_snap.fall_cancelled) {
                // 跌倒未取消：进入地址锁定
                s_force_address = true;
                s_display_mode = DISPLAY_MODE_ADDRESS;
            } else {
                // 其他报警或跌倒已取消：恢复正常
                s_display_mode = DISPLAY_MODE_NORMAL;
            }
            s_current_alert_reason[0] = '\0';
        } else if (s_display_mode == DISPLAY_MODE_ADDRESS) {
            // 从地址页手动切回正常，解除所有锁定
            s_display_mode = DISPLAY_MODE_NORMAL;
            s_force_address = false;
            s_current_alert_reason[0] = '\0';
        } else {
            s_display_mode = DISPLAY_MODE_ADDRESS;
        }
    }

    /* ---------- 4. 弹窗超时（使用本地原因副本） ---------- */
    if (s_display_mode == DISPLAY_MODE_ALERT) {
        uint32_t timeout = (strcmp(s_current_alert_reason, "Fall") == 0) ? FALL_ALERT_MS : ALERT_POPUP_MS;

        if (now - s_alert_start_tick >= timeout) {
            if (osMutexAcquire(xDataMutexHandle, 100) == osOK) {
                g_SystemStatus.popup_pending = false;
                osMutexRelease(xDataMutexHandle);
            }

            if (strcmp(s_current_alert_reason, "Fall") == 0) {
                s_force_address = true;
                s_display_mode = DISPLAY_MODE_ADDRESS;
            } else {
                s_display_mode = DISPLAY_MODE_NORMAL;
            }
            s_current_alert_reason[0] = '\0';
        }
    }

    /* 地址锁定：只要 s_force_address 为真，且跌倒未被取消，强制显示地址 */
    if (s_force_address && !status_snap.fall_cancelled) {
        s_display_mode = DISPLAY_MODE_ADDRESS;
    }
    // 若跌倒已被取消，地址锁定自动解除
    if (status_snap.fall_cancelled) {
        s_force_address = false;
    }

    /* 局部状态清理：如果系统完全正常且不在任何特殊模式，可清除本地 reason 但保留上面逻辑即可 */

    /* ---------- 5. 绘制 ---------- */
    SH1106_Clear();

    if (s_display_mode == DISPLAY_MODE_ALERT) {
        if (strcmp(s_current_alert_reason, "Fall") != 0) {
            SH1106_DrawUTF8String(1, 32, s_alert_line1);
            if (s_alert_line2[0] != '\0')
                SH1106_DrawUTF8String(5, 32, s_alert_line2);
        } else {
            SH1106_DrawUTF8String(1, 28, s_alert_line1);
            if (s_alert_line2[0] != '\0')
                SH1106_DrawUTF8String(3, 16, s_alert_line2);
            if (s_alert_line3[0] != '\0')
                SH1106_DrawUTF8String(5, 12, s_alert_line3);
        }
    } else if (s_display_mode == DISPLAY_MODE_ADDRESS) {
        SH1106_DrawUTF8String(0, 0, "地址：");
        if (family_snap.valid && family_snap.address[0] != '\0')
            SH1106_DrawUTF8StringWrap(2, 0, family_snap.address);
        else
            SH1106_DrawUTF8String(2, 0, "无地址信息");
    } else {
        if (sensor_snap.temp_valid)
            snprintf(line1, sizeof(line1), "体温：%.1f ℃", sensor_snap.temperature);
        else
            strcpy(line1, "体温：--- ℃");
        if (sensor_snap.hr_valid)
            snprintf(line2, sizeof(line2), "心率：%d 次/分", sensor_snap.heart_rate);
        else
            strcpy(line2, "心率：--- 次/分");
        if (sensor_snap.hr_valid)
            snprintf(line3, sizeof(line3), "血氧：%d %%", sensor_snap.spo2);
        else
            strcpy(line3, "血氧：-- %");
        snprintf(line4, sizeof(line4), "步数：%lu 步", sensor_snap.step);
        line1[sizeof(line1)-1] = '\0';
        line2[sizeof(line2)-1] = '\0';
        line3[sizeof(line3)-1] = '\0';
        line4[sizeof(line4)-1] = '\0';
        SH1106_DrawUTF8String(0, 0, line1);
        SH1106_DrawUTF8String(2, 0, line2);
        SH1106_DrawUTF8String(4, 0, line3);
        SH1106_DrawUTF8String(6, 0, line4);
    }

    SH1106_Update();
}

void HMITask(void *argument)
{
    osDelay(500);
    SH1106_Init(&hi2c1);
    SH1106_Clear();
    SH1106_DrawUTF8String(3, 24, "启动中...");
    SH1106_Update();

    Button_Init();
    osDelay(1000);

    // 用于显示刷新的计数器
    uint32_t display_tick = 0;
    // 定义显示刷新间隔：例如每 10 次循环刷新一次（10 * 20ms = 200ms）
    const uint32_t display_refresh_interval = 10; 

    for (;;) {
        // 高频按键扫描（每 20ms 一次，保证响应）
        Button_Scan();
        
        // 低频显示刷新（每 200ms 一次）
        display_tick++;
        if (display_tick >= display_refresh_interval) {
            HMI_DisplayLoop();
            display_tick = 0;
        }
        
        osDelay(20);
    }
}