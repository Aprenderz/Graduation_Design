/*
 * mpu6050.c
 *
 * Simple MPU6050 helper implementation (No Mutex - Dedicated I2C2)
 * 修改：移除互斥锁，直接访问 I2C 外设
 */

#include "mpu6050.h"
#include <math.h>

#define MPU6050_I2C_ADDR         (0x68 << 1) /* HAL expects 8-bit address */

/* MPU6050 register definitions */
#define MPU6050_REG_PWR_MGMT_1   0x6B
#define MPU6050_REG_SMPLRT_DIV   0x19
#define MPU6050_REG_CONFIG       0x1A
#define MPU6050_REG_GYRO_CONFIG  0x1B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B

/* Sensitivity scale factor for accelerometer (±2g) */
#define MPU6050_ACCEL_SENSITIVITY 16384.0f
/* Sensitivity for gyroscope (±250 deg/s) */
#define MPU6050_GYRO_SENSITIVITY  131.0f

/* --- 内部辅助函数 --- */

static bool mpu6050_write_reg(I2C_HandleTypeDef* hi2c, uint8_t reg, uint8_t value)
{
    // 直接写入，不再申请互斥锁
    if (HAL_I2C_Mem_Write(hi2c, MPU6050_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &value, 1, 100) == HAL_OK)
        return true;
    return false;
}

/* 底层无锁读取，供上层封装使用 */
static bool mpu6050_read_bytes_no_lock(I2C_HandleTypeDef* hi2c, uint8_t reg, uint8_t* buf, uint16_t len)
{
    // 假设调用者已经持有了 xI2CMutexHandle
    if (HAL_I2C_Mem_Read(hi2c, MPU6050_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, buf, len, 200) == HAL_OK)
        return true;
    return false;
}

/* --- 公开接口实现 --- */

bool MPU6050_Init(I2C_HandleTypeDef* hi2c)
{
    if (!mpu6050_write_reg(hi2c, MPU6050_REG_PWR_MGMT_1, 0x00)) return false;
    if (!mpu6050_write_reg(hi2c, MPU6050_REG_SMPLRT_DIV, 0x00)) return false;
    if (!mpu6050_write_reg(hi2c, MPU6050_REG_CONFIG, 0x00)) return false;
    if (!mpu6050_write_reg(hi2c, MPU6050_REG_GYRO_CONFIG, 0x00)) return false;
    if (!mpu6050_write_reg(hi2c, MPU6050_REG_ACCEL_CONFIG, 0x00)) return false;
    
    HAL_Delay(10);
    return true;
}

/**
 * @brief 新增：带锁的原始数据读取
 */
bool MPU6050_ReadRawBytes(I2C_HandleTypeDef* hi2c, uint8_t* buf)
{
    // 直接读取，不再申请互斥锁
    if (mpu6050_read_bytes_no_lock(hi2c, MPU6050_REG_ACCEL_XOUT_H, buf, 14))
        return true;
    
    return false;
}

/**
 * @brief 新增：无锁解析并直接填充到系统结构体
 */
void MPU6050_ParseAndFill_SystemData(const uint8_t* raw_buf, SensorData_t* pSysData)
{
    int16_t raw_ax = (int16_t)(raw_buf[0] << 8 | raw_buf[1]);
    int16_t raw_ay = (int16_t)(raw_buf[2] << 8 | raw_buf[3]);
    int16_t raw_az = (int16_t)(raw_buf[4] << 8 | raw_buf[5]);
    
    int16_t raw_gx = (int16_t)(raw_buf[8] << 8 | raw_buf[9]);
    int16_t raw_gy = (int16_t)(raw_buf[10] << 8 | raw_buf[11]);
    int16_t raw_gz = (int16_t)(raw_buf[12] << 8 | raw_buf[13]);

    // 直接操作传入的全局结构体指针
    pSysData->ax = raw_ax / MPU6050_ACCEL_SENSITIVITY;
    pSysData->ay = raw_ay / MPU6050_ACCEL_SENSITIVITY;
    pSysData->az = raw_az / MPU6050_ACCEL_SENSITIVITY;

    pSysData->gx = raw_gx / MPU6050_GYRO_SENSITIVITY;
    pSysData->gy = raw_gy / MPU6050_GYRO_SENSITIVITY;
    pSysData->gz = raw_gz / MPU6050_GYRO_SENSITIVITY;
    
    // 温度通常由 DS18B20 提供，此处可根据需要选择是否更新 pSysData->temperature
}