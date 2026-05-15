/*
 * system_data.h
 * 全局传感器数据共享结构定义
 * 架构原则：
 * 1. Sensor Task  -> 写入 SensorData_t (原始数据)
 * 2. Decision Task -> 读取 SensorData_t, 写入 SystemStatus_t (决策结果)
 * 3. HMI Task     -> 读取 SystemStatus_t/SensorData_t, 写入 HMIInput_t (用户输入)
 * 4. Comm Task    -> 读取 SystemStatus_t, 清零 need_upload 标志
 */

#ifndef __SYSTEM_DATA_H__
#define __SYSTEM_DATA_H__

#include <stdint.h>
#include <stdbool.h>
#include <string.h> // 用于 memset/strcpy
#include <sys/_intsup.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================= 1. 传感器原始数据区 ================= */
/* 生产者: Sensor Task | 消费者: Decision Task, HMI Task */
typedef struct {
    // 数据时间戳 (单位: ms)，用于检测数据是否过期
    uint32_t last_update_tick; 

    // MPU6050 姿态数据
    float ax, ay, az; // 加速度 (g)
    float gx, gy, gz; // 角速度 (deg/s)
    bool mpu_valid;   // 数据有效性标志 (硬件读取成功)

    // DS18B20 体温数据
    float temperature;
    bool temp_valid;      // 温度数据是否有效
    
    // MAX30102
    uint8_t heart_rate;
    uint8_t spo2;
    bool hr_valid;

    // --- GPS 定位数据（ATGM336H） ---
    float latitude;
    float longitude;
    uint32_t step;
    bool gps_valid;
    uint32_t gps_last_update_tick; /* HAL_GetTick() */

} SensorData_t;

/* ================= 2. 系统决策状态区 ================= */
/* 生产者: Decision Task | 消费者: HMI Task, Comm Task */
typedef enum {
    SYS_STATE_NORMAL,       // 正常
    SYS_STATE_WARNING,      // 报警 (如体温过高、心率异常)
    SYS_STATE_FALL_ALARM,   // 跌倒报警 (最高优先级)
    SYS_STATE_SOS_ALARM,    // SOS 触发报警
    SYS_STATE_LOW_BATTERY   // 低电量
} SystemState_t;

typedef struct {
    SystemState_t state;
    uint32_t alarm_timestamp; // 报警触发时间戳 (HAL_GetTick)
    
    bool need_upload;         // [Comm Task 清零] 需要立即上传云端
    char alarm_reason[18];   // 报警原因
    bool fall_cancelled;     // 跌倒报警是否已取消
    bool sedentary_enabled;  // 久坐提醒是否已启用
    char alert_message[32];
    bool popup_pending;      // 报警弹窗请求，HMI 负责清除
} SystemStatus_t;

/* ================= 3. 人机交互输入区 ================= */
/* 生产者: HMI Task | 消费者: Decision Task (如长按 SOS) */
typedef struct {
    // SOS 按键（PB12 ）：短按连按 5 次触发 SOS
    uint8_t sos_short_press_count; /* 连按计数，由 HMI 更新；Decision 触发后清零 */
    bool sos_long_press;            /* 长按 10s 事件（由 HMI 置 1，一次性） */

    // 功能按键（PB14 ）：点击OLED切换显示地址, 地址为字符串，再按一次切回正常显示
    bool oled_switch_press;        /* 按键按下事件 */
    char *oled_switch_address;    /* 地址信息 */
    bool fun_long_press;          /* 功能键长按事件 */

} HMIInput_t;

/*================== 4. 家庭配置区 ================= */
typedef struct {
    char phone[20];         // 家庭成员电话
    char address[128];      // 家庭地址
    bool valid;             // 是否已配置
} FamilyConfig_t;

/* ================= 全局实例声明 ================= */
// 请在 freertos.c 或其他主 .c 文件中定义这些变量，并初始化为 0
extern SensorData_t g_SensorData;
extern SystemStatus_t g_SystemStatus;
extern HMIInput_t g_HMIInput;
extern FamilyConfig_t g_FamilyConfig;

// 辅助宏：检查数据是否过期 (阈值设为 2000ms)
// 注意：使用此宏需确保包含 main.h 以获取 HAL_GetTick()
#define IS_DATA_FRESH(data_struct) ((HAL_GetTick() - (data_struct).last_update_tick) < 2000)

#ifdef __cplusplus
}
#endif

#endif /* __SYSTEM_DATA_H__ */