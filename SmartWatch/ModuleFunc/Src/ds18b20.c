#include "ds18b20.h"
#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/* ---------- 体表->核心体温映射表与插值 ---------- */
#define SURFACE_TEMP_MIN    250   // 25.0°C * 10
#define SURFACE_TEMP_MAX    400   // 40.0°C * 10
#define CORE_TEMP_MIN       360   // 36.0°C * 10
#define CORE_TEMP_MAX       390   // 39.0°C * 10

/* 使用 uint16_t 存储 0.1°C 单位的温度值 */
static const uint16_t auc_core_temp_table[] = {
    // 体表 25.0 ~ 27.9 → 核心 36.2 ~ 36.5（缓慢上升）
    362, 362, 362, 362, 362, 362, 362, 362, 362, 362,
    363, 363, 363, 363, 363, 363, 363, 363, 363, 363,
    364, 364, 364, 364, 364, 364, 364, 364, 365, 365,
    // 体表 28.0 ~ 36.0 → 核心 36.5 ~ 36.8（平稳区，拉宽）
    365, 365, 365, 365, 365, 365, 365, 365, 365, 365,
    365, 365, 365, 365, 365, 365, 365, 365, 366, 366,
    366, 366, 366, 366, 366, 366, 366, 366, 366, 366,
    366, 366, 366, 366, 367, 367, 367, 367, 367, 367,
    367, 367, 367, 367, 367, 367, 367, 367, 367, 367,
    368, 368, 368, 368, 368, 368, 368, 368, 368, 368,
    368, 368, 368, 368, 368, 368, 368, 368, 368, 368,
    368, 368, 368, 368, 368, 368, 368, 368, 368, 368,
    368, 368, 368, 368, 368, 368, 368, 368, 368, 368,
    368, 368, 368, 368, 368, 368, 368, 368, 368, 368,
    368, 368, 368, 368, 368, 368, 368, 368, 368, 368,
    // 体表 36.1 ~ 39.9 → 核心 36.9 ~ 39.0（加速饱和）
    369, 369, 369, 370, 370, 371, 371, 372, 372, 373,
    373, 374, 374, 375, 375, 376, 376, 377, 378, 378,
    379, 379, 380, 381, 381, 382, 383, 383, 384, 385,
    386, 386, 387, 388, 389, 389, 390, 390, 390, 390
};
#define TABLE_SIZE (sizeof(auc_core_temp_table) / sizeof(auc_core_temp_table[0]))

// 1-Wire 时序要求较严格，这里使用 DWT 周期延时
static void DWT_DelayUs(uint32_t us)
{
    // 确保 DWT 计数器已使能
    uint32_t clk_hz = HAL_RCC_GetHCLKFreq();
    uint32_t cycles = (clk_hz / 1000000u) * us;
    uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles)
    {
        // busy wait
    }
}

static void DS_DQ_LOW(void)
{
    HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_RESET);
}

static void DS_DQ_RELEASE(void)
{
    // open-drain：置高=释放，由外部上拉拉高
    HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_SET);
}

static bool DS_ReadBit(void)
{
    bool bit;

    // 关中断，保证微秒级时序不被中断破坏
    __disable_irq();

    DS_DQ_LOW();
    DWT_DelayUs(2);      // >1us
    DS_DQ_RELEASE();
    DWT_DelayUs(8);      // 等待采样点 ~15us
    bit = (HAL_GPIO_ReadPin(DS18B20_PORT, DS18B20_PIN) == GPIO_PIN_SET);
    DWT_DelayUs(60);     // 保证时序完成

    __enable_irq();
    return bit;
}

static void DS_WriteBit(bool bit)
{
    __disable_irq();

    if (bit)
    {
        // 写 1：低电平 1-15us，释放
        DS_DQ_LOW();
        DWT_DelayUs(6);
        DS_DQ_RELEASE();
        DWT_DelayUs(64); // 保证时序完成
    }
    else
    {
        // 写 0：低电平 60us
        DS_DQ_LOW();
        DWT_DelayUs(60);
        DS_DQ_RELEASE();
        DWT_DelayUs(10);
    }

    __enable_irq();
}

static void DS_WriteByte(uint8_t byte)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        DS_WriteBit((byte >> i) & 0x01);
    }
}

static uint8_t DS_ReadByte(void)
{
    uint8_t value = 0;
    for (uint8_t i = 0; i < 8; i++)
    {
        if (DS_ReadBit())
        {
            value |= (1 << i);
        }
    }
    return value;
}

static bool DS_ResetPulse(void)
{
    bool presence;

    __disable_irq();

    DS_DQ_LOW();
    DWT_DelayUs(750);
    DS_DQ_RELEASE();
    DWT_DelayUs(45);
    presence = (HAL_GPIO_ReadPin(DS18B20_PORT, DS18B20_PIN) == GPIO_PIN_RESET);
    DWT_DelayUs(240);

    __enable_irq();
    return presence;
}

static uint16_t SurfaceToCoreTempInt(float surface_temp)
{
    int surf10 = (int)(surface_temp * 10.0f + 0.5f);
    if (surf10 < 250) return 0;           // 无效
    if (surf10 >= 400) return 390;
    int idx = surf10 - 250;
    if (idx >= TABLE_SIZE - 1) return auc_core_temp_table[TABLE_SIZE - 1];
    uint16_t core_low = auc_core_temp_table[idx];
    uint16_t core_high = auc_core_temp_table[idx + 1];
    int frac = surf10 - (250 + idx);      // 0~9
    return (uint16_t)((core_low * (10 - frac) + core_high * frac + 5) / 10);
}

bool DS18B20_Init(void)
{
    // 使能 DWT 计数器
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DS_DQ_RELEASE();

    return DS_ResetPulse();
}

void DS18B20_StartConversion(void)
{
    // Convert T：750ms 完成
    if (!DS_ResetPulse()) return;   
    DS_WriteByte(0xCC); // Skip ROM
    DS_WriteByte(0x44); // Convert T
}

bool DS18B20_ReadTemp(float* temperature)
{
    uint8_t lsb, msb;
    int16_t raw_temp;
    float surface;

    if (!temperature) return false;
    if (!DS_ResetPulse()) return false;     // 复位、设备未响应

    DS_WriteByte(0xCC); // Skip ROM
    DS_WriteByte(0xBE); // Read Scratchpad

    // 读取 2 字节温度数据
    lsb = DS_ReadByte();
    msb = DS_ReadByte();

    // 数据处理
    raw_temp = (msb << 8) | lsb;
    
    // DS18B20 默认 12-bit 分辨率：0.0625 度/LSB
    surface = (int)(raw_temp * 0.0625f * 10.0f + 0.5f) / 10.0f;

    if (surface < 25.0f) return false;
    uint16_t core_int = SurfaceToCoreTempInt(surface);
    *temperature = core_int / 10.0f;
    return true;

    return true;
}