#include "gps_atgm336h.h"
#include "main.h"
#include <string.h>
#include <stdlib.h>

/* 环形缓冲区 */
#define RX_BUF_SIZE 512
static uint8_t s_rx_buf[RX_BUF_SIZE];
static volatile uint32_t s_rx_head = 0;   // 写入索引
static volatile uint32_t s_rx_tail = 0;   // 读取索引
static uint8_t gps_rx_byte;

/* 行缓存 */
static char s_line[128];
static uint32_t s_line_len = 0;

/* 存储最新 GPS 数据（无锁） */
typedef struct {
    float latitude;
    float longitude;
    bool  valid;
    uint32_t last_tick;
} GPS_Data_t;
static GPS_Data_t s_latest_gps = {0.0f, 0.0f, false, 0};

/* 向环形缓冲区写入一个字节（中断中调用） */
void GPS_ATGM336H_RxChar(uint8_t ch)
{
    uint32_t next = (s_rx_head + 1) % RX_BUF_SIZE;
    if (next != s_rx_tail) {
        s_rx_buf[s_rx_head] = ch;
        s_rx_head = next;
    }
}

/* 从环形缓冲区读取一个字节（任务中调用，非阻塞） */
static bool rx_getchar(uint8_t *ch)
{
    if (s_rx_head == s_rx_tail) return false;
    *ch = s_rx_buf[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1) % RX_BUF_SIZE;
    return true;
}

/* 解析 RMC 语句（无调试打印版） */
static bool parse_rmc_sentence(char* line)
{
    if (!line || line[0] != '$') return false;

    // 检查是否包含 RMC（快速过滤）
    if (strstr(line, "RMC") == NULL) return false;

    // 复制副本用于 strtok
    char tmp[128];
    strncpy(tmp, line, sizeof(tmp)-1);
    tmp[sizeof(tmp)-1] = '\0';

    // 分割字段
    char* token = strtok(tmp, ",");
    if (!token) return false;

    int field_cnt = 0;
    char* fields[15] = {0};
    while ((token = strtok(NULL, ",")) != NULL && field_cnt < 15) {
        fields[field_cnt++] = token;
    }

    // RMC 标准字段至少需要 6 个（time, status, lat, lat_dir, lon, lon_dir）
    if (field_cnt < 6) return false;

    char* status  = fields[1];
    char* lat_str = fields[2];
    char* lat_dir = fields[3];
    char* lon_str = fields[4];
    char* lon_dir = fields[5];

    if (!status || !lat_str || !lat_dir || !lon_str || !lon_dir)
        return false;

    bool valid = (status[0] == 'A');

    // 经纬度为空时仅更新有效标志
    if (lat_str[0] == '\0' || lon_str[0] == '\0') {
        s_latest_gps.valid = valid;
        s_latest_gps.last_tick = HAL_GetTick();
        return true;
    }

    // 转换纬度
    double lat_raw = atof(lat_str);
    double lon_raw = atof(lon_str);
    int lat_deg = (int)(lat_raw / 100.0);
    double lat_min = lat_raw - (double)lat_deg * 100.0;
    double lat = (double)lat_deg + (lat_min / 60.0);
    if (lat_dir[0] == 'S' || lat_dir[0] == 's') lat = -lat;

    // 转换经度
    int lon_deg = (int)(lon_raw / 100.0);
    double lon_min = lon_raw - (double)lon_deg * 100.0;
    double lon = (double)lon_deg + (lon_min / 60.0);
    if (lon_dir[0] == 'W' || lon_dir[0] == 'w') lon = -lon;

    s_latest_gps.latitude  = (float)lat;
    s_latest_gps.longitude = (float)lon;
    s_latest_gps.valid     = valid;
    s_latest_gps.last_tick = HAL_GetTick();

    return true;
}

/* 初始化 GPS 模块，清空缓冲区 */
bool GPS_ATGM336H_Init(void)
{
    s_rx_head = 0;
    s_rx_tail = 0;
    s_line_len = 0;
    HAL_UART_Receive_IT(&huart2, &gps_rx_byte, 1);
    return true;
}

/*
 * 任务中调用：只处理最新一条完整的 RMC 语句
 * 快速遍历缓冲区，丢弃非 RMC 行，只保留最后一条 RMC 并解析
 */
void GPS_ATGM336H_Poll(void)
{
    uint8_t ch;
    char latest_rmc[128] = {0};
    bool has_rmc = false;

    // 一次性清空缓冲区，只为找出最后一条 RMC 语句
    while (rx_getchar(&ch)) {
        if (ch == '\n') {
            s_line[s_line_len] = '\0';
            if (s_line_len > 10) {
                // 如果是 RMC 语句，则覆盖保存
                if (strstr(s_line, "RMC") != NULL) {
                    strncpy(latest_rmc, s_line, sizeof(latest_rmc) - 1);
                    latest_rmc[sizeof(latest_rmc) - 1] = '\0';
                    has_rmc = true;
                }
            }
            s_line_len = 0;
        } else if (ch == '\r') {
            // 忽略回车
        } else {
            if (s_line_len < sizeof(s_line) - 1) {
                s_line[s_line_len++] = (char)ch;
            } else {
                // 行过长，丢弃
                s_line_len = 0;
            }
        }
    }

    // 如果存在 RMC 行，只解析最后一条
    if (has_rmc) {
        parse_rmc_sentence(latest_rmc);
    }
}

/* 获取最新 GPS 数据（供 SensorTask 调用） */
void GPS_ATGM336H_GetLatest(float *lat, float *lon, bool *valid)
{
    if (lat)  *lat  = s_latest_gps.latitude;
    if (lon)  *lon  = s_latest_gps.longitude;
    if (valid)*valid = s_latest_gps.valid;
}

/* UART 接收完成回调函数（中断中调用） */
void GPS_ATGM336H_RxCpltCallback_ISR(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        GPS_ATGM336H_RxChar(gps_rx_byte);
        HAL_UART_Receive_IT(&huart2, &gps_rx_byte, 1);
    }
}