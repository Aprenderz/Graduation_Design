/*
 * button.c
 * 按键驱动实现
 * - 基于HAL库读取GPIO状态
 * - 内部状态机实现去抖、短按、长按、连按计数
 * - 更新全局变量 g_HMIInput（需获取互斥锁 xDataMutexHandle）
 */

#include "button.h"
#include "main.h"
#include <string.h>

/* 按键GPIO定义（请根据实际硬件修改） */
#define SOS_BUTTON_PIN      GPIO_PIN_12
#define SOS_BUTTON_PORT     GPIOB
#define FUNC_BUTTON_PIN     GPIO_PIN_14
#define FUNC_BUTTON_PORT    GPIOB

/* 去抖及长按参数 */
#define DEBOUNCE_MS         20      // 扫描周期（ms）
#define LONG_PRESS_TIME     3000    // 长按3秒（ms）
#define SOS_SHORT_PRESS_COUNT_TARGET 5   // 连按5次触发SOS

/* 按键状态机状态 */
typedef enum {
    BTN_STATE_IDLE,
    BTN_STATE_PRESSED,
    BTN_STATE_LONG_PRESS_TRIGGERED,
    BTN_STATE_WAIT_RELEASE
} ButtonState_t;

/* 内部变量 */
static uint32_t s_sos_press_start = 0;      // 按下时刻（tick）
static uint32_t s_func_press_start = 0;
static ButtonState_t s_sos_state = BTN_STATE_IDLE;
static ButtonState_t s_func_state = BTN_STATE_IDLE;

/* 连按计数器（记录短按次数） */
static uint8_t s_sos_short_count = 0;
static uint32_t s_last_sos_release_tick = 0;
#define SHORT_PRESS_TIMEOUT_MS  2000        // 2秒内连按有效

/* 内部函数：SOS按键短按处理（累加计数，达到5次则触发） */
static void SOS_HandleShortPress(void)
{
    uint32_t now = HAL_GetTick();
    
    // 检查是否在连按有效时间内
    if ((now - s_last_sos_release_tick) > SHORT_PRESS_TIMEOUT_MS) {
        s_sos_short_count = 0;  // 超时重置计数
    }
    s_sos_short_count++;
    s_last_sos_release_tick = now;

    // 更新全局变量
    if (osMutexAcquire(xDataMutexHandle, 100) == osOK) {
        g_HMIInput.sos_short_press_count = s_sos_short_count;
        // 由 Alert 任务检测到计数≥5后清零，此处不主动清零
        osMutexRelease(xDataMutexHandle);
    }
}

/* 内部函数：SOS长按触发（仅设置标志，并由 HMI 任务处理后清零） */
static void SOS_HandleLongPress(void)
{
    if (osMutexAcquire(xDataMutexHandle, 100) == osOK) {
        g_HMIInput.sos_long_press = true;
        // 长按发生时立即清空连按计数（避免残留计数干扰）
        s_sos_short_count = 0;
        g_HMIInput.sos_short_press_count = 0;
        osMutexRelease(xDataMutexHandle);
    }
}

/* 内部函数：功能按键短按（切换显示模式） */
static void FUNC_HandleShortPress(void)
{
    if (osMutexAcquire(xDataMutexHandle, 100) == osOK) {
        g_HMIInput.oled_switch_press = true;
        osMutexRelease(xDataMutexHandle);
    }
}

/* 初始化GPIO */
void Button_Init(void)
{
    // GPIO已在CubeMX中配置为输入模式，此处无需重复初始化
    s_sos_state = BTN_STATE_IDLE;
    s_func_state = BTN_STATE_IDLE;
    s_sos_short_count = 0;
    s_last_sos_release_tick = 0;
}

/*
 * 按键扫描函数，需在HMI任务中以固定周期（如20ms）调用
 */
void Button_Scan(void)
{
    uint32_t now = HAL_GetTick();
    bool sos_level = (HAL_GPIO_ReadPin(SOS_BUTTON_PORT, SOS_BUTTON_PIN) == GPIO_PIN_SET);
    bool func_level = (HAL_GPIO_ReadPin(FUNC_BUTTON_PORT, FUNC_BUTTON_PIN) == GPIO_PIN_SET);

    /* ---------- SOS按键状态机 ---------- */
    switch (s_sos_state) {
        case BTN_STATE_IDLE:
            if (sos_level == GPIO_PIN_RESET) {  // 按下（低电平有效）
                s_sos_press_start = now;
                s_sos_state = BTN_STATE_PRESSED;
            }
            break;

        case BTN_STATE_PRESSED:
            if (sos_level == GPIO_PIN_SET) {    // 提前释放 -> 短按
                SOS_HandleShortPress();
                s_sos_state = BTN_STATE_IDLE;
            } else if ((now - s_sos_press_start) >= LONG_PRESS_TIME) {
                // 长按达到阈值
                SOS_HandleLongPress();
                s_sos_state = BTN_STATE_LONG_PRESS_TRIGGERED;
            }
            break;

        case BTN_STATE_LONG_PRESS_TRIGGERED:
            // 等待释放
            if (sos_level == GPIO_PIN_SET) {
                s_sos_state = BTN_STATE_IDLE;
            }
            break;

        default:
            s_sos_state = BTN_STATE_IDLE;
            break;
    }

    /* ---------- 功能按键状态机 ---------- */
    switch (s_func_state) {
        case BTN_STATE_IDLE:
            if (func_level == GPIO_PIN_RESET) {
                s_func_press_start = now;
                s_func_state = BTN_STATE_PRESSED;
            }
            break;

        case BTN_STATE_PRESSED:
            if (func_level == GPIO_PIN_SET) {                // 释放 → 短按
                FUNC_HandleShortPress();
                s_func_state = BTN_STATE_IDLE;
            } else if ((now - s_func_press_start) >= LONG_PRESS_TIME) {   // 复用 3 秒
                // 长按触发
                if (osMutexAcquire(xDataMutexHandle, 100) == osOK) {
                    g_HMIInput.fun_long_press = true;
                    osMutexRelease(xDataMutexHandle);
                }
                s_func_state = BTN_STATE_LONG_PRESS_TRIGGERED;
            }
            break;

        case BTN_STATE_LONG_PRESS_TRIGGERED:
            // 等待释放，不做额外动作
            if (func_level == GPIO_PIN_SET) {
                s_func_state = BTN_STATE_IDLE;
            }
            break;

        default:
            s_func_state = BTN_STATE_IDLE;
            break;
    }
}