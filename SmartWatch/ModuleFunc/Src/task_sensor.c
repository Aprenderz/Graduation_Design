/*
 * task_sensor.c
 * 传感器任务：数据采集与更新
 * 锁策略： 数据锁 (更新全局变量)
 */

#include "task_sensor.h"
#include "ds18b20.h"
#include "mpu6050.h"
#include "gps_atgm336h.h"
#include <math.h>
#include "i2c.h"
#include "stm32f4xx_hal_gpio.h"

/* 私有状态标志 */
static bool s_ds18b20_ok = false;
static bool s_mpu_ok = false;
static bool s_gps_ok = false;

/* DS18B20 状态机定义 */
typedef enum {
    DS_STATE_START_CONV,   // 状态 0: 启动转换
    DS_STATE_WAITING,      // 状态 1: 等待转换完成
    DS_STATE_READ_DATA     // 状态 2: 读取数据
} DS18B20_State_t;

/* DS18B20 私有变量 */
static DS18B20_State_t s_ds_state = DS_STATE_START_CONV;
static uint32_t s_ds_start_tick = 0;
const uint32_t DS_CONVERSION_TIME_MS = 800; // 留一点余量，标称 750ms

/* ---------- 计步器私有变量 ---------- */
#define PED_STEP_THRESHOLD   1.15f   // 合加速度阈值 (g)
#define PED_MIN_INTERVAL_MS  280     // 最小步间隔 (ms)
#define PED_FILTER_SIZE      4       // 滑动平均窗口

static float s_ped_mag_history[PED_FILTER_SIZE] = {0};
static uint8_t s_ped_history_idx = 0;
static bool s_ped_above_threshold = false;  // 上次状态
static uint32_t s_ped_last_step_tick = 0;
static uint32_t s_ped_step_count = 0;

/**
 * @brief 独立函数：读取并同步 DS18B20
 */
static void Read_DS18B20(void)
{
    switch (s_ds_state)
    {
        case DS_STATE_START_CONV:
            if (s_ds18b20_ok)
            {
                DS18B20_StartConversion();
                s_ds_start_tick = HAL_GetTick();
                s_ds_state = DS_STATE_WAITING;
            }
            break;

        case DS_STATE_WAITING:
            // 检查是否已过足够时间
            if (HAL_GetTick() - s_ds_start_tick >= DS_CONVERSION_TIME_MS)
            {
                s_ds_state = DS_STATE_READ_DATA;
                // 不 break，直接 fallthrough 到 READ_DATA 执行读取，提高效率
                // 或者为了逻辑清晰，也可以 let it break 并在下一次循环读取
                // 这里选择直接执行读取
            }
            else break;

        case DS_STATE_READ_DATA:
        {
            float temp_val = 0.0f;
            bool read_success = false;

            read_success = DS18B20_ReadTemp(&temp_val);

            // 获取数据锁以更新全局结构体
            if (osMutexAcquire(xDataMutexHandle, portMAX_DELAY) == osOK)
            {
                if (read_success)
                {
                    g_SensorData.temperature = temp_val;
                    g_SensorData.temp_valid = true;
                    g_SensorData.last_update_tick = HAL_GetTick();
                }
                else
                    g_SensorData.temp_valid = false;   // 读取失败（可能是总线干扰），标记无效，但不清零旧值以免跳变
                osMutexRelease(xDataMutexHandle);
            }

            // 无论成功失败，一轮结束，回到开始状态
            s_ds_state = DS_STATE_START_CONV;
            break;
        }
        
        default:
            s_ds_state = DS_STATE_START_CONV;
            break;
    }
}

/**
 * @brief 计步处理（内部函数）
 */
static bool Pedometer_Process(float acc_magnitude, uint32_t tick)
{
    // 1. 滑动平均滤波
    s_ped_mag_history[s_ped_history_idx] = acc_magnitude;
    s_ped_history_idx = (s_ped_history_idx + 1) % PED_FILTER_SIZE;
    float sum = 0;
    for (int i = 0; i < PED_FILTER_SIZE; i++) {
        sum += s_ped_mag_history[i];
    }
    float avg = sum / PED_FILTER_SIZE;

    // 2. 上升沿检测
    bool is_above = (avg > PED_STEP_THRESHOLD);
    bool step_detected = false;

    if (is_above && !s_ped_above_threshold) {
        if ((tick - s_ped_last_step_tick) > PED_MIN_INTERVAL_MS) {
            s_ped_step_count++;
            s_ped_last_step_tick = tick;
            step_detected = true;
        }
    }
    s_ped_above_threshold = is_above;
    return step_detected;
}

/**
 * @brief 独立函数：读取并同步 MPU6050
 * 
 * 顺序严格执行：
 * 1. 调用 MPU6050_ReadRawBytes -> 得到局部 buffer
 * 2. 获取 xDataMutexHandle (数据锁)
 * 3. 调用 MPU6050_ParseAndFill_SystemData (直接写入 g_SensorData)
 * 4. 更新标志位
 * 5. 释放 xDataMutexHandle
 */
static void Read_MPU6050(void)
{
    if (!s_mpu_ok) return;

    uint8_t raw_buffer[14];
    bool io_success = false;

    io_success = MPU6050_ReadRawBytes(&hi2c2, raw_buffer);
    
    // 无论 IO 是否成功，我们都需要更新数据锁区域，以便标记状态或清零数据
    if (osMutexAcquire(xDataMutexHandle, portMAX_DELAY) == osOK)
    {
        if (io_success)
        {
            // 直接解析并填入 g_SensorData，无中间结构体拷贝
            MPU6050_ParseAndFill_SystemData(raw_buffer, &g_SensorData);
            
            g_SensorData.mpu_valid = true;
            g_SensorData.last_update_tick = HAL_GetTick();

             // 计步：计算合加速度，更新步数
            float mag = sqrtf(g_SensorData.ax * g_SensorData.ax +
                              g_SensorData.ay * g_SensorData.ay +
                              g_SensorData.az * g_SensorData.az);
            if (Pedometer_Process(mag, HAL_GetTick())) {
                g_SensorData.step = s_ped_step_count;  // 同步到全局结构体
            }
        }
        else
        {
            // 通信失败：标记无效并清零，防止使用旧数据
            g_SensorData.mpu_valid = false;
            g_SensorData.ax = 0.0f;
            g_SensorData.ay = 0.0f;
            g_SensorData.az = 0.0f;
            g_SensorData.gx = 0.0f;
            g_SensorData.gy = 0.0f;
            g_SensorData.gz = 0.0f;
            // 失败时不更新 last_update_tick，以便外部检测超时
        }

        osMutexRelease(xDataMutexHandle);
    }
    else
    {
        // 如果连数据锁都拿不到（极罕见），返回失败
        return;
    }
}

/* 独立函数：读取 GPS 并更新全局数据（遵循与其他模块相同的模式） */
static void Read_GPS(void)
{
    // 检查模块是否初始化成功
    if (!s_gps_ok)
        return;

    // 轮询环形缓冲区，处理接收到的字符并解析
    GPS_ATGM336H_Poll();

    // 获取最新 GPS 数据（从驱动内部静态变量读取）
    float lat, lon;
    bool valid;
    GPS_ATGM336H_GetLatest(&lat, &lon, &valid);

    // 加锁更新全局结构体
    if (osMutexAcquire(xDataMutexHandle, portMAX_DELAY) == osOK)
    {
        if (valid)
        {
            g_SensorData.latitude  = lat;
            g_SensorData.longitude = lon;
            g_SensorData.gps_valid = true;
            g_SensorData.gps_last_update_tick = HAL_GetTick();
        }
        else
            g_SensorData.gps_valid = false;
        osMutexRelease(xDataMutexHandle);
    }
}

/**
 * @brief 传感器任务入口
 */
void SensorTask(void *argument)
{
    /* 初始化延迟 */
    osDelay(200);

    /* 初始化传感器 */ 
    s_ds18b20_ok = DS18B20_Init();
    s_mpu_ok = MPU6050_Init(&hi2c2);
    s_gps_ok = GPS_ATGM336H_Init();

    uint32_t last_gps_tick = 0;   // 记录上次 GPS 更新的时间戳
    uint32_t last_temp_tick = 0;

    for (;;)
    {
        uint32_t now = HAL_GetTick();

        Read_MPU6050();    // 加速计保持实时采集

        // 每 5 秒启动一次温度采集流程
        if (now - last_temp_tick >= 5000) {
            Read_DS18B20();
            last_temp_tick = now;
        }

        // GPS 每分钟一次
        if (now - last_gps_tick >= 30000) {
            Read_GPS();
            last_gps_tick = now;
        }

        osDelay(50);
    }
}