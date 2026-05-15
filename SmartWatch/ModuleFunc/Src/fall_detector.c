/*
 * fall_detector.c
 * 跌倒检测算法核心实现 (纯算法)
 */

#include "fall_detector.h"
#include <math.h>

/* -------------------------------------------------------------------------- */
/*                              算法阈值配置                                  */
/* -------------------------------------------------------------------------- */
#define FALL_FREEFALL        0.8f   // 合加速度低于此值视为失重
#define FALL_IMPACT          1.5f   // 合加速度高于此值视为撞击（降低以适应缓冲）
#define FALL_TILT_ANGLE     30.0f   // 倾斜角超过此值视为倒地姿态（降低要求）
#define FALL_STILL           0.3f   // 合加速度与 1g 的差值小于此值视为静止（放宽容差）

#define FALL_WINDOW_TIME     300    // 失重到撞击的最大允许时间（ms）
#define FALL_STILL_DURATION 2000    // 撞击后需保持静止的时长（ms）

/* -------------------------------------------------------------------------- */
/*                              状态机状态定义                                */
/* -------------------------------------------------------------------------- */
#define STATE_IDLE       0          // 空闲状态：等待失重事件
#define STATE_FREEFALL   1          // 失重状态：已检测到失重，等待撞击
#define STATE_IMPACT     2          // 撞击状态：已检测到撞击，等待静止与倾斜
#define STATE_FALL       3          // 跌倒确认：已触发报警，状态锁定

/* -------------------------------------------------------------------------- */
/*                              内部辅助计算                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief 计算合加速度 magnitude
 */
static float Calc_Accel_Magnitude(float ax, float ay, float az)
{
    return sqrtf(ax * ax + ay * ay + az * az);
}

/**
 * @brief 计算倾斜角 (相对于重力向量 Z 轴，静止时有效)
 */
static float Calc_Tilt_Angle(float ax, float ay, float az)
{
    float mag = Calc_Accel_Magnitude(ax, ay, az);
    if (mag < 0.1f) return 0.0f;
    
    float cos_theta = az / mag;
    if (cos_theta > 1.0f) cos_theta = 1.0f;
    if (cos_theta < -1.0f) cos_theta = -1.0f;
    
    float theta_rad = acosf(cos_theta);
    return theta_rad * (180.0f / 3.1415926f);
}

/* -------------------------------------------------------------------------- */
/*                              公开接口实现                                  */
/* -------------------------------------------------------------------------- */

void FallDetector_Init(FallDetector_t* detector)
{
    if (detector == NULL) return;
    detector->state = STATE_IDLE;
    detector->freefall_tick = 0;
    detector->impact_tick = 0;
    detector->alarm_triggered = false;
}

bool FallDetector_Process(FallDetector_t* detector, const SensorData_t* data, uint32_t current_tick)
{
    if (detector == NULL || data == NULL) return false;

    if (!data->mpu_valid) return false;

    float ax = data->ax;
    float ay = data->ay;
    float az = data->az;
    
    float acc_mag = Calc_Accel_Magnitude(ax, ay, az);
    float tilt_angle = Calc_Tilt_Angle(ax, ay, az);
    
    bool is_fall_detected = false;

    switch (detector->state)
    {
        case STATE_IDLE:
            if (acc_mag < FALL_FREEFALL)
            {
                detector->state = STATE_FREEFALL;
                detector->freefall_tick = current_tick;
            }
            break;

        case STATE_FREEFALL:
            if ((current_tick - detector->freefall_tick) > FALL_WINDOW_TIME)
                detector->state = STATE_IDLE;
            else if (acc_mag > FALL_IMPACT)
            {
                detector->state = STATE_IMPACT;
                detector->impact_tick = current_tick;
            }
            else if (acc_mag > 0.8f && acc_mag < 1.2f)
                detector->state = STATE_IDLE;
            break;

        case STATE_IMPACT:
            if ((current_tick - detector->impact_tick) > FALL_STILL_DURATION)
            {
                if (fabsf(acc_mag - 1.0f) < FALL_STILL && tilt_angle > FALL_TILT_ANGLE)
                {
                    detector->state = STATE_FALL;
                    detector->alarm_triggered = false;
                }
                else detector->state = STATE_IDLE;
            }
            break;

        case STATE_FALL:
            if (!detector->alarm_triggered)
            {
                detector->alarm_triggered = true;
                is_fall_detected = true;
            }
            break;
            
        default:
            detector->state = STATE_IDLE;
            break;
    }

    return is_fall_detected;
}

void FallDetector_Reset(FallDetector_t* detector)
{
    if (detector == NULL) return;
    detector->state = STATE_IDLE;
    detector->alarm_triggered = false;
}