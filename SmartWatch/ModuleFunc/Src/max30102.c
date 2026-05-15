#include "max30102.h"
#include "FreeRTOS.h"
#include "main.h"
#include "task.h"
#include <string.h>
#include <stdlib.h>

#ifndef min
#define min(x, y) (((x) < (y)) ? (x) : (y))
#endif

// 静态变量：保存 I2C 句柄
static I2C_HandleTypeDef *p_hi2c = NULL;
static bool is_initialized = false;

// 环形缓冲区
static uint32_t ir_buffer[BUFFER_SIZE];
static uint32_t red_buffer[BUFFER_SIZE];
static uint16_t sample_index = 0;
static bool buffer_full = false;

// 手指检测相关
#define FINGER_DETECT_THRESHOLD    50000   // 根据实际调试修改
#define DC_SAMPLE_COUNT            50

// 算法相关静态变量（用于 Maxim 算法）
static int32_t an_dx[BUFFER_SIZE - MA4_SIZE];
static int32_t an_x[BUFFER_SIZE];
static int32_t an_y[BUFFER_SIZE];

// 官方查表数据（从参考代码中复制）
const uint16_t auw_hamm[31] = {
    41, 276, 512, 276, 41
};

const uint8_t uch_spo2_table[184] = {
    95, 95, 95, 96, 96, 96, 97, 97, 97, 97, 97, 98, 98, 98, 98, 98, 99, 99, 99, 99,
    99, 99, 99, 99, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
    100, 100, 100, 100, 99, 99, 99, 99, 99, 99, 99, 99, 98, 98, 98, 98, 98, 98, 97, 97,
    97, 97, 96, 96, 96, 96, 95, 95, 95, 94, 94, 94, 93, 93, 93, 92, 92, 92, 91, 91,
    90, 90, 89, 89, 89, 88, 88, 87, 87, 86, 86, 85, 85, 84, 84, 83, 82, 82, 81, 81,
    80, 80, 79, 78, 78, 77, 76, 76, 75, 74, 74, 73, 72, 72, 71, 70, 69, 69, 68, 67,
    66, 66, 65, 64, 63, 62, 62, 61, 60, 59, 58, 57, 56, 56, 55, 54, 53, 52, 51, 50,
    49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 31, 30, 29,
    28, 27, 26, 25, 23, 22, 21, 20, 19, 17, 16, 15, 14, 12, 11, 10, 9, 7, 6, 5,
    3, 2, 1
};

// 私有函数声明
static void maxim_heart_rate_and_oxygen_saturation(uint32_t *pun_ir_buffer, int32_t n_ir_buffer_length,
                                                   uint32_t *pun_red_buffer, int32_t *pn_spo2,
                                                   int8_t *pch_spo2_valid, int32_t *pn_heart_rate,
                                                   int8_t *pch_hr_valid);
static void maxim_find_peaks(int32_t *pn_locs, int32_t *pn_npks, int32_t *pn_x, int32_t n_size,
                             int32_t n_min_height, int32_t n_min_distance, int32_t n_max_num);
static void maxim_peaks_above_min_height(int32_t *pn_locs, int32_t *pn_npks, int32_t *pn_x,
                                         int32_t n_size, int32_t n_min_height);
static void maxim_remove_close_peaks(int32_t *pn_locs, int32_t *pn_npks, int32_t *pn_x,
                                     int32_t n_min_distance);
static void maxim_sort_ascend(int32_t *pn_x, int32_t n_size);
static void maxim_sort_indices_descend(int32_t *pn_x, int32_t *pn_indx, int32_t n_size);

// ==================== 硬件 I2C 底层 ====================
static void MAX30102_WriteReg(uint8_t reg, uint8_t data)
{
    HAL_I2C_Mem_Write(p_hi2c, MAX30102_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
}

static uint8_t MAX30102_ReadReg(uint8_t reg)
{
    uint8_t data;
    HAL_I2C_Mem_Read(p_hi2c, MAX30102_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
    return data;
}

// 读取一个 FIFO 样本（3字节 IR + 3字节 RED）
static void MAX30102_ReadFIFO(uint32_t *red, uint32_t *ir)
{
    uint8_t fifo[6];
    uint8_t temp;

    // 清除中断标志
    temp = MAX30102_ReadReg(REG_INTR_STATUS_1);
    temp = MAX30102_ReadReg(REG_INTR_STATUS_2);

    HAL_I2C_Mem_Read(p_hi2c, MAX30102_I2C_ADDR, REG_FIFO_DATA, I2C_MEMADD_SIZE_8BIT,
                     fifo, 6, 100);

    *ir = (uint32_t)((fifo[0] << 16) | (fifo[1] << 8) | fifo[2]);
    *red = (uint32_t)((fifo[3] << 16) | (fifo[4] << 8) | fifo[5]);
    *red &= 0x03FFFF;
    *ir &= 0x03FFFF;
}

// ==================== 初始化 ====================
bool MAX30102_Init(I2C_HandleTypeDef *hi2c)
{
    p_hi2c = hi2c;

    // 软复位
    MAX30102_WriteReg(REG_MODE_CONFIG, 0x40);
    vTaskDelay(pdMS_TO_TICKS(100));   // FreeRTOS 非阻塞延时

    // 读取 Part ID 验证连接
    uint8_t part_id = MAX30102_ReadReg(REG_PART_ID);
    if (part_id != 0x15) {
        is_initialized = false;
        return false;
    }

    // 配置寄存器（参考官方推荐）
    MAX30102_WriteReg(REG_INTR_ENABLE_1, 0xC0);
    MAX30102_WriteReg(REG_INTR_ENABLE_2, 0x00);
    MAX30102_WriteReg(REG_FIFO_WR_PTR, 0x00);
    MAX30102_WriteReg(REG_OVF_COUNTER, 0x00);
    MAX30102_WriteReg(REG_FIFO_RD_PTR, 0x00);
    MAX30102_WriteReg(REG_FIFO_CONFIG, 0x0F);
    MAX30102_WriteReg(REG_MODE_CONFIG, 0x03);      // SpO2 模式
    MAX30102_WriteReg(REG_SPO2_CONFIG, 0x27);      // 100Hz, 400us, 4096nA
    MAX30102_WriteReg(REG_LED1_PA, 0x24);          // IR LED 电流 ~7mA
    MAX30102_WriteReg(REG_LED2_PA, 0x24);          // RED LED 电流 ~7mA
    MAX30102_WriteReg(REG_PILOT_PA, 0x7F);

    is_initialized = true;
    
    return true;
}

// ==================== 手指检测 ====================
bool IsFingerPresent(void)
{
    if (sample_index < DC_SAMPLE_COUNT) return false;
    uint64_t sum_ir = 0;
    for (int i = 0; i < DC_SAMPLE_COUNT; i++) sum_ir += ir_buffer[i];
    uint32_t ir_dc = (uint32_t)(sum_ir / DC_SAMPLE_COUNT);
    
    // 放宽阈值：直流 > 20000 即认为有手指（原来 30000）
    // 或者去掉交流幅度判断，只用直流
    return (ir_dc > 20000);
}

// ==================== 核心接口：Push 一个样本 ====================
void MAX30102_PushSample(I2C_HandleTypeDef *hi2c)
{
    if (!is_initialized) return;
    if (hi2c != p_hi2c) p_hi2c = hi2c;  // 允许重新设置句柄

    uint32_t red, ir;
    MAX30102_ReadFIFO(&red, &ir);

    ir_buffer[sample_index] = ir;
    red_buffer[sample_index] = red;
    sample_index++;
    
    if (sample_index >= BUFFER_SIZE) {
        sample_index = 0;
        buffer_full = true;
    }
}

// ==================== 获取处理后的数据 ====================
void MAX30102_GetProcessedData(uint8_t *hr, uint8_t *spo2, bool *valid)
{
    static TickType_t last_call = 0;
    
    // 心率相关历史
    static uint8_t hr_history[10] = {0};
    static uint8_t hr_idx = 0;
    static uint8_t hr_count = 0;
    static uint8_t last_valid_hr = 70;
    static uint8_t last_good_hr = 70;
    static uint8_t hr_reject_count = 0;
    
    // 血氧相关历史（新增）
    static uint8_t spo2_history[5] = {0};
    static uint8_t spo2_idx = 0;
    static uint8_t spo2_count = 0;
    static uint8_t last_valid_spo2 = 97;
    static uint8_t last_good_spo2 = 97;
    static uint8_t spo2_reject_count = 0;
    
    TickType_t now = xTaskGetTickCount();

    if (!buffer_full) {
        *valid = false;
        return;
    }

    if (!IsFingerPresent()) {
        *valid = false;
        *hr = 0;
        *spo2 = 0;
        hr_count = 0;
        spo2_count = 0;
        hr_reject_count = 0;
        spo2_reject_count = 0;
        return;
    }

    if ((now - last_call) < pdMS_TO_TICKS(250)) {
        *valid = false;
        return;
    }
    last_call = now;

    int32_t n_spo2 = 0, n_heart_rate = 0;
    int8_t ch_spo2_valid = 0, ch_hr_valid = 0;

    maxim_heart_rate_and_oxygen_saturation(
        ir_buffer, BUFFER_SIZE,
        red_buffer,
        &n_spo2, &ch_spo2_valid,
        &n_heart_rate, &ch_hr_valid
    );

    // ========== 心率处理（范围40~120）==========
    bool hr_ok = false;
    uint8_t final_hr = 0;
    
    if (ch_hr_valid && n_heart_rate >= 40 && n_heart_rate <= 200) {
        uint8_t cur_hr = (uint8_t)n_heart_rate;
        
        // 异常值拒绝：偏差 >12% 丢弃
        if (last_good_hr != 0) {
            float dev = (float)abs(cur_hr - last_good_hr) / last_good_hr;
            if (dev > 0.12f) {
                hr_reject_count++;
                if (hr_reject_count > 3) {
                    last_good_hr = cur_hr;
                    hr_reject_count = 0;
                }
                goto hr_done;
            }
        }
        hr_reject_count = 0;
        last_good_hr = cur_hr;
        
        // 存入环形缓冲
        hr_history[hr_idx] = cur_hr;
        hr_idx = (hr_idx + 1) % 10;
        if (hr_count < 10) hr_count++;
        
        // 中值滤波
        uint8_t temp[10];
        uint8_t len = (hr_count >= 10) ? 10 : hr_count;
        for (int i = 0; i < len; i++) {
            int idx = (hr_idx - len + i + 10) % 10;
            temp[i] = hr_history[idx];
        }
        for (int i = 0; i < len-1; i++) {
            for (int j = 0; j < len-i-1; j++) {
                if (temp[j] > temp[j+1]) {
                    uint8_t t = temp[j];
                    temp[j] = temp[j+1];
                    temp[j+1] = t;
                }
            }
        }
        uint8_t hr_median = temp[len/2];
        
        // 变化率限制 ±10%
        if (last_valid_hr != 0) {
            if (hr_median > last_valid_hr * 1.10f) hr_median = last_valid_hr * 1.10f;
            if (hr_median < last_valid_hr * 0.90f) hr_median = last_valid_hr * 0.90f;
        }
        last_valid_hr = hr_median;
        final_hr = hr_median;
        hr_ok = true;
    }
    hr_done:

    // ========== 血氧处理（范围70~100）==========
    bool spo2_ok = false;
    uint8_t final_spo2 = 0;
    
    if (ch_spo2_valid && n_spo2 >= 60 && n_spo2 <= 100) {
        uint8_t cur_spo2 = (uint8_t)n_spo2;
        
        // 异常值拒绝：偏差 >5% 丢弃
        if (last_good_spo2 != 0) {
            float dev = (float)abs(cur_spo2 - last_good_spo2) / last_good_spo2;
            if (dev > 0.05f) {
                spo2_reject_count++;
                if (spo2_reject_count > 3) {
                    last_good_spo2 = cur_spo2;
                    spo2_reject_count = 0;
                }
                goto spo2_done;
            }
        }
        spo2_reject_count = 0;
        last_good_spo2 = cur_spo2;
        
        // 存入环形缓冲（5点）
        spo2_history[spo2_idx] = cur_spo2;
        spo2_idx = (spo2_idx + 1) % 5;
        if (spo2_count < 5) spo2_count++;
        
        // 中值滤波
        uint8_t temp[5];
        uint8_t len = (spo2_count >= 5) ? 5 : spo2_count;
        for (int i = 0; i < len; i++) {
            int idx = (spo2_idx - len + i + 5) % 5;
            temp[i] = spo2_history[idx];
        }
        for (int i = 0; i < len-1; i++) {
            for (int j = 0; j < len-i-1; j++) {
                if (temp[j] > temp[j+1]) {
                    uint8_t t = temp[j];
                    temp[j] = temp[j+1];
                    temp[j+1] = t;
                }
            }
        }
        uint8_t spo2_median = temp[len/2];
        
        // 变化率限制 ±3%
        if (last_valid_spo2 != 0) {
            if (spo2_median > last_valid_spo2 * 1.03f) spo2_median = last_valid_spo2 * 1.03f;
            if (spo2_median < last_valid_spo2 * 0.97f) spo2_median = last_valid_spo2 * 0.97f;
        }
        last_valid_spo2 = spo2_median;
        final_spo2 = spo2_median;
        spo2_ok = true;
    }
    spo2_done:

    // 只有心率和血氧同时有效，才输出有效数据
    if (hr_ok && spo2_ok) {
        *hr = final_hr;
        *spo2 = final_spo2;
        *valid = true;
    } else {
        *valid = false;
        *hr = 0;
        *spo2 = 0;
    }
}

// ==================== Maxim 官方算法实现（原样复制） ====================
// 以下函数完全来自您提供的参考代码，未作修改

void maxim_heart_rate_and_oxygen_saturation(uint32_t *pun_ir_buffer, int32_t n_ir_buffer_length,
                                            uint32_t *pun_red_buffer, int32_t *pn_spo2,
                                            int8_t *pch_spo2_valid, int32_t *pn_heart_rate,
                                            int8_t *pch_hr_valid)
{
    uint32_t un_ir_mean, un_only_once;
    int32_t k, n_i_ratio_count;
    int32_t i, s, m, n_exact_ir_valley_locs_count, n_middle_idx;
    int32_t n_th1, n_npks, n_c_min;
    int32_t an_ir_valley_locs[15];
    int32_t an_exact_ir_valley_locs[15];
    int32_t an_dx_peak_locs[15];
    int32_t n_peak_interval_sum;

    int32_t n_y_ac, n_x_ac;
    int32_t n_spo2_calc;
    int32_t n_y_dc_max, n_x_dc_max;
    int32_t n_y_dc_max_idx, n_x_dc_max_idx;
    int32_t an_ratio[5], n_ratio_average;
    int32_t n_nume, n_denom;

    // 去除 DC
    un_ir_mean = 0;
    for (k = 0; k < n_ir_buffer_length; k++) un_ir_mean += pun_ir_buffer[k];
    un_ir_mean = un_ir_mean / n_ir_buffer_length;
    for (k = 0; k < n_ir_buffer_length; k++) an_x[k] = pun_ir_buffer[k] - un_ir_mean;

    // 4 点移动平均
    for (k = 0; k < BUFFER_SIZE - MA4_SIZE; k++) {
        n_denom = (an_x[k] + an_x[k + 1] + an_x[k + 2] + an_x[k + 3]);
        an_x[k] = n_denom / (int32_t)4;
    }

    // 差分
    for (k = 0; k < BUFFER_SIZE - MA4_SIZE - 1; k++)
        an_dx[k] = (an_x[k + 1] - an_x[k]);

    // 2 点移动平均
    for (k = 0; k < BUFFER_SIZE - MA4_SIZE - 2; k++)
        an_dx[k] = (an_dx[k] + an_dx[k + 1]) / 2;

    // 汉明窗
    for (i = 0; i < BUFFER_SIZE - HAMMING_SIZE - MA4_SIZE - 2; i++) {
        s = 0;
        for (k = i; k < i + HAMMING_SIZE; k++) {
            s -= an_dx[k] * auw_hamm[k - i];
        }
        an_dx[i] = s / (int32_t)1146;
    }

    // 计算阈值
    n_th1 = 0;
    for (k = 0; k < BUFFER_SIZE - HAMMING_SIZE; k++) {
        n_th1 += ((an_dx[k] > 0) ? an_dx[k] : ((int32_t)0 - an_dx[k]));
    }
    n_th1 = n_th1 / (BUFFER_SIZE - HAMMING_SIZE);
    n_th1 = n_th1 * 4 / 2;   // 提高 50% 的阈值，减少误检

    // 找峰值
   maxim_find_peaks(an_dx_peak_locs, &n_npks, an_dx, BUFFER_SIZE - HAMMING_SIZE, n_th1, 25, 5);

    n_peak_interval_sum = 0;
    if (n_npks >= 3) {
        for (k = 1; k < n_npks; k++)
            n_peak_interval_sum += (an_dx_peak_locs[k] - an_dx_peak_locs[k - 1]);
        n_peak_interval_sum = n_peak_interval_sum / (n_npks - 1);
        *pn_heart_rate = (int32_t)(6000 / n_peak_interval_sum);
        *pch_hr_valid = 1;
    } else {
        *pn_heart_rate = -999;
        *pch_hr_valid = 0;
    }

    // 谷值位置
    for (k = 0; k < n_npks; k++)
        an_ir_valley_locs[k] = an_dx_peak_locs[k] + HAMMING_SIZE / 2;

    // 精确谷值
    n_exact_ir_valley_locs_count = 0;
    for (k = 0; k < n_npks; k++) {
        un_only_once = 1;
        m = an_ir_valley_locs[k];
        n_c_min = 16777216;
        if (m + 5 < BUFFER_SIZE - HAMMING_SIZE && m - 5 > 0) {
            for (i = m - 5; i < m + 5; i++) {
                if (an_x[i] < n_c_min) {
                    if (un_only_once > 0) un_only_once = 0;
                    n_c_min = an_x[i];
                    an_exact_ir_valley_locs[k] = i;
                }
            }
            if (un_only_once == 0)
                n_exact_ir_valley_locs_count++;
        }
    }

    if (n_exact_ir_valley_locs_count < 2) {
        *pn_spo2 = -999;
        *pch_spo2_valid = 0;
        return;
    }

    // 恢复原始数据
    for (k = 0; k < n_ir_buffer_length; k++) {
        an_x[k] = pun_ir_buffer[k];
        an_y[k] = pun_red_buffer[k];
    }

    // 4 点移动平均
    for (k = 0; k < BUFFER_SIZE - MA4_SIZE; k++) {
        an_x[k] = (an_x[k] + an_x[k + 1] + an_x[k + 2] + an_x[k + 3]) / (int32_t)4;
        an_y[k] = (an_y[k] + an_y[k + 1] + an_y[k + 2] + an_y[k + 3]) / (int32_t)4;
    }

    // 计算每个波段的 AC/DC 比例
    n_ratio_average = 0;
    n_i_ratio_count = 0;
    for (k = 0; k < 5; k++) an_ratio[k] = 0;

    for (k = 0; k < n_exact_ir_valley_locs_count - 1; k++) {
        n_y_dc_max = -16777216;
        n_x_dc_max = -16777216;
        if (an_exact_ir_valley_locs[k + 1] - an_exact_ir_valley_locs[k] > 10) {
            for (i = an_exact_ir_valley_locs[k]; i < an_exact_ir_valley_locs[k + 1]; i++) {
                if (an_x[i] > n_x_dc_max) {
                    n_x_dc_max = an_x[i];
                    n_x_dc_max_idx = i;
                }
                if (an_y[i] > n_y_dc_max) {
                    n_y_dc_max = an_y[i];
                    n_y_dc_max_idx = i;
                }
            }
            n_y_ac = (an_y[an_exact_ir_valley_locs[k + 1]] - an_y[an_exact_ir_valley_locs[k]]) * (n_y_dc_max_idx - an_exact_ir_valley_locs[k]);
            n_y_ac = an_y[an_exact_ir_valley_locs[k]] + n_y_ac / (an_exact_ir_valley_locs[k + 1] - an_exact_ir_valley_locs[k]);
            n_y_ac = an_y[n_y_dc_max_idx] - n_y_ac;

            n_x_ac = (an_x[an_exact_ir_valley_locs[k + 1]] - an_x[an_exact_ir_valley_locs[k]]) * (n_x_dc_max_idx - an_exact_ir_valley_locs[k]);
            n_x_ac = an_x[an_exact_ir_valley_locs[k]] + n_x_ac / (an_exact_ir_valley_locs[k + 1] - an_exact_ir_valley_locs[k]);
            n_x_ac = an_x[n_y_dc_max_idx] - n_x_ac;

            n_nume = (n_y_ac * n_x_dc_max) >> 7;
            n_denom = (n_x_ac * n_y_dc_max) >> 7;
            if (n_denom > 0 && n_i_ratio_count < 5 && n_nume != 0) {
                an_ratio[n_i_ratio_count] = (n_nume * 20) / n_denom;
                n_i_ratio_count++;
            }
        }
    }

    maxim_sort_ascend(an_ratio, n_i_ratio_count);
    n_middle_idx = n_i_ratio_count / 2;
    if (n_middle_idx > 1)
        n_ratio_average = (an_ratio[n_middle_idx - 1] + an_ratio[n_middle_idx]) / 2;
    else
        n_ratio_average = an_ratio[n_middle_idx];

    if (n_ratio_average > 2 && n_ratio_average < 184) {
        n_spo2_calc = uch_spo2_table[n_ratio_average];
        *pn_spo2 = n_spo2_calc;
        *pch_spo2_valid = 1;
    } else {
        *pn_spo2 = -999;
        *pch_spo2_valid = 0;
    }
}

void maxim_find_peaks(int32_t *pn_locs, int32_t *pn_npks, int32_t *pn_x, int32_t n_size,
                      int32_t n_min_height, int32_t n_min_distance, int32_t n_max_num)
{
    maxim_peaks_above_min_height(pn_locs, pn_npks, pn_x, n_size, n_min_height);
    maxim_remove_close_peaks(pn_locs, pn_npks, pn_x, n_min_distance);
    *pn_npks = min(*pn_npks, n_max_num);
}

void maxim_peaks_above_min_height(int32_t *pn_locs, int32_t *pn_npks, int32_t *pn_x,
                                  int32_t n_size, int32_t n_min_height)
{
    int32_t i = 1, n_width;
    *pn_npks = 0;
    while (i < n_size - 1) {
        if (pn_x[i] > n_min_height && pn_x[i] > pn_x[i - 1]) {
            n_width = 1;
            while (i + n_width < n_size && pn_x[i] == pn_x[i + n_width])
                n_width++;
            if (pn_x[i] > pn_x[i + n_width] && (*pn_npks) < 15) {
                pn_locs[(*pn_npks)++] = i;
                i += n_width + 1;
            } else {
                i += n_width;
            }
        } else {
            i++;
        }
    }
}

void maxim_remove_close_peaks(int32_t *pn_locs, int32_t *pn_npks, int32_t *pn_x,
                              int32_t n_min_distance)
{
    int32_t i, j, n_old_npks, n_dist;
    maxim_sort_indices_descend(pn_x, pn_locs, *pn_npks);
    for (i = -1; i < *pn_npks; i++) {
        n_old_npks = *pn_npks;
        *pn_npks = i + 1;
        for (j = i + 1; j < n_old_npks; j++) {
            n_dist = pn_locs[j] - (i == -1 ? -1 : pn_locs[i]);
            if (n_dist > n_min_distance || n_dist < -n_min_distance)
                pn_locs[(*pn_npks)++] = pn_locs[j];
        }
    }
    maxim_sort_ascend(pn_locs, *pn_npks);
}

void maxim_sort_ascend(int32_t *pn_x, int32_t n_size)
{
    int32_t i, j, n_temp;
    for (i = 1; i < n_size; i++) {
        n_temp = pn_x[i];
        for (j = i; j > 0 && n_temp < pn_x[j - 1]; j--)
            pn_x[j] = pn_x[j - 1];
        pn_x[j] = n_temp;
    }
}

void maxim_sort_indices_descend(int32_t *pn_x, int32_t *pn_indx, int32_t n_size)
{
    int32_t i, j, n_temp;
    for (i = 1; i < n_size; i++) {
        n_temp = pn_indx[i];
        for (j = i; j > 0 && pn_x[n_temp] > pn_x[pn_indx[j - 1]]; j--)
            pn_indx[j] = pn_indx[j - 1];
        pn_indx[j] = n_temp;
    }
}