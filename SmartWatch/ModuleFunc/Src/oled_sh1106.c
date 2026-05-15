/*
 * oled_sh1106.c
 *
 * Minimal SH1106 driver for 128x64 displays.
 * MODIFIED: Removed all Mutex handling. Direct I2C Access.
 */

#include "oled_sh1106.h"
#include "oled_font_chinese.h"
#include "main.h"      // 仅用于获取 I2C 句柄定义
#include <string.h>

#define SH1106_I2C_ADDR  (0x3C << 1)
#define CHINESE_MAP_SIZE  (sizeof(s_chinese_map) / sizeof(s_chinese_map[0]))

/* Each page is 128 bytes, 8 pages for 64px height */
static uint8_t s_buffer[SH1106_WIDTH * (SH1106_HEIGHT / 8)];
static uint8_t s_tx_buffer[1 + SH1106_WIDTH]; // 全局缓冲区（不占用栈）
static I2C_HandleTypeDef* s_hi2c = NULL;

// ---------- 汉字映射表（UTF-8字符串 -> 字库索引） ----------
typedef struct {
    const char *utf8;      // UTF-8 编码的汉字字符串（如 "体"）
    uint16_t index;        // 在 ChineseFont_16x16 中的索引
} ChineseMap_t;

// 映射表必须与 oled_font_chinese.c 中的字库顺序完全对应！
static const ChineseMap_t s_chinese_map[] = {
    {"体", 0}, {"温", 1}, {"心", 2}, {"率", 3}, {"血", 4}, {"氧", 5},
    {"是", 6}, {"否", 7}, {"跌", 8}, {"倒", 9}, {"监", 10}, {"测", 11},
    {"到", 12}, {"异", 13}, {"常", 14}, {"过", 15}, {"低", 16}, {"高", 17},
    {"家", 18}, {"庭", 19}, {"信", 20}, {"息", 21}, {"地", 22}, {"址", 23},
    {"省", 24}, {"市", 25}, {"区", 26}, {"县", 27}, {"镇", 28}, {"乡", 29},
    {"村", 30}, {"组", 31}, {"号", 32}, {"电", 33}, {"话", 34}, {"福", 35},
    {"建", 36}, {"莆", 37}, {"田", 38}, {"城", 39}, {"厢", 40}, {"兴", 41},
    {"路", 42}, {"四", 43}, {"川", 44}, {"广", 45}, {"安", 46}, {"武", 47},
    {"胜", 48}, {"猛", 49}, {"山", 50}, {"州", 51}, {"仓", 52}, {"南", 53},
    {"江", 54}, {"滨", 55}, {"西", 56}, {"大", 57}, {"道", 58}, {"：", 59},
    {"启", 60}, {"动", 61}, {"中", 62}, {"国", 63}, {"无", 64}, {"厦", 65},
    {"门", 66}, {"成", 67}, {"都", 68}, {"北", 69}, {"京", 70}, {"重", 71},
    {"庆", 72}, {"荔", 73}, {"秀", 74}, {"屿", 75}, {"学", 76}, {"园", 77},
    {"街", 78}, {"次", 79}, {"分", 80}, {"℃", 81}, {"即", 82}, {"将", 83},
    {"拨", 84}, {"打", 85}, {"长", 86}, {"按", 87}, {"键", 88}, {"取", 89}, 
    {"消", 90}, {"已", 91}, {"步", 92}, {"数", 93}, {"久", 94}, {"坐", 95},
    {"提", 96}, {"醒", 97}, {"请", 98}, {"适", 99}, {"当", 100}, {"活", 101},
    {"开", 102}, {"关", 103}, {"闭", 104},
};

/**
 * @brief 内部写命令函数 (无锁)
 */
static bool sh1106_write_cmd_unlocked(uint8_t cmd)
{
    uint8_t data[2] = {0x00, cmd}; // 0x00 = Command mode
    if (s_hi2c == NULL) return false;
    return HAL_I2C_Master_Transmit(s_hi2c, SH1106_I2C_ADDR, data, sizeof(data), 100) == HAL_OK;
}

/**
 * @brief 设置页和列地址
 */
static void sh1106_set_page_column_unlocked(uint8_t page, uint8_t column)
{
    /* SH1106 expects column address offset by 2 */
    uint8_t col = column + 2;
    
    sh1106_write_cmd_unlocked(0xB0 | (page & 0x07));
    sh1106_write_cmd_unlocked(0x00 | (col & 0x0F));
    sh1106_write_cmd_unlocked(0x10 | ((col >> 4) & 0x0F));
}

/* 8x16 font for ASCII characters (0x20..0x7E) */
static const uint8_t s_font8x16[][16] = {
#include "oled_sh1106_font8x16.inc"
};



bool SH1106_Init(I2C_HandleTypeDef* hi2c)
{
    s_hi2c = hi2c;

    const uint8_t init_cmds[] = {
        0xAE, /* display off */
        0xD5, 0x80, /* set display clock divide */
        0xA8, 0x3F, /* multiplex 1/64 */
        0xD3, 0x00, /* display offset */
        0x40, /* set start line */
        0x8D, 0x14, /* enable charge pump */
        0x20, 0x00, /* horizontal addressing mode */
        0xA1, /* segment remap */
        0xC8, /* COM output scan direction */
        0xDA, 0x12, /* COM pins hardware configuration */
        0x81, 0x7F, /* contrast */
        0xD9, 0xF1, /* pre-charge */
        0xDB, 0x40, /* VCOMH deselect level */
        0xA4, /* disable entire display on */
        0xA6, /* normal display */
        0xAF /* display on */
    };

    // 直接循环发送初始化指令
    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        if (!sh1106_write_cmd_unlocked(init_cmds[i]))
            return false;
    }

    // 清屏 (直接操作缓冲区并更新)
    memset(s_buffer, 0, sizeof(s_buffer));
    
    // 初始化阶段也需要直接更新，不再通过带锁的接口
    // 这里复用 SH1106_Update 的逻辑，或者直接写一个简单的发送循环
    for (uint8_t page = 0; page < (SH1106_HEIGHT / 8); page++) {
        sh1106_set_page_column_unlocked(page, 0);
        
        uint8_t tx[1 + SH1106_WIDTH];
        tx[0] = 0x40; // 0x40 = Data mode
        memset(&tx[1], 0, SH1106_WIDTH);
        
        if (HAL_I2C_Master_Transmit(s_hi2c, SH1106_I2C_ADDR, tx, sizeof(tx), 200) != HAL_OK) {
            // 简单的错误处理
        }
    }

    return true;
}

void SH1106_Clear(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
}

/**
 * @brief 更新屏幕显示 (无锁)
 * @note 直接将缓冲区数据刷入 OLED
 */

void SH1106_Update(void) {
    if (s_hi2c == NULL) return;

    for (uint8_t page = 0; page < (SH1106_HEIGHT / 8); page++) {
        sh1106_set_page_column_unlocked(page, 0);
        
        // 复用全局缓冲区
        s_tx_buffer[0] = 0x40; /* Data control byte */
        memcpy(&s_tx_buffer[1], &s_buffer[page * SH1106_WIDTH], SH1106_WIDTH);

        if (HAL_I2C_Master_Transmit(s_hi2c, SH1106_I2C_ADDR, s_tx_buffer, sizeof(s_tx_buffer), 200) != HAL_OK) {
            // 建议添加错误处理：尝试复位I2C或OLED
            // Example: I2C_Reset(s_hi2c);
        }
    }
}

/**
 * @brief 绘制单个 8x16 ASCII 字符（纵向逐行式取模）
 * @param page  起始页（0~6）
 * @param col   起始列（0~127）
 * @param ch    ASCII 字符（0x20~0x7E）
 */
void SH1106_DrawChar(uint8_t page, uint8_t col, char ch)
{
    if (ch < 0x20 || ch > 0x7E)
        return;
    if (page >= (SH1106_HEIGHT / 8) || col >= SH1106_WIDTH)
        return;
    if (col + 8 > SH1106_WIDTH)
        return;

    uint8_t idx = (uint8_t)ch - 0x20;
    const uint8_t *font = s_font8x16[idx];

    // 纵向逐行式取模:
    // 前8字节(font[0]~font[7]) = 所有列的上半部分
    // 后8字节(font[8]~font[15]) = 所有列的下半部分
    // 第i列: 上半=font[i], 下半=font[i+8]
    for (uint8_t i = 0; i < 8; i++) {
        uint8_t upper = font[i];     // 第i列的上半字节 (行0~7)
        uint8_t lower = font[i + 8]; // 第i列的下半字节 (行8~15)

        // 列写入 buffer
        uint16_t buf_idx = page * SH1106_WIDTH + col + i;

        // 第一页: 上半字节
        s_buffer[buf_idx] = upper;

        // 第二页: 下半字节
        uint8_t page2 = page + 1;
        if (page2 < (SH1106_HEIGHT / 8)) {
            s_buffer[page2 * SH1106_WIDTH + col + i] = lower;
        }
    }
}

/**
 * @brief 绘制 ASCII 字符串（8x16字体）
 * @param page  起始页（0~6）
 * @param col   起始列
 * @param str   字符串
 */
void SH1106_DrawString(uint8_t page, uint8_t col, const char* str)
{
    while (*str) {
        if (col + 8 > SH1106_WIDTH)
            break;
        SH1106_DrawChar(page, col, *str);
        col += 9;  // 8像素宽 + 1像素间距
        str++;
    }
}

// 根据 UTF-8 汉字字符串查找字库索引，未找到返回 -1
static int16_t find_chinese_index(const char *utf8_char)
{
    for (size_t i = 0; i < CHINESE_MAP_SIZE; i++) {
        if (strcmp(utf8_char, s_chinese_map[i].utf8) == 0) {
            return (int16_t)s_chinese_map[i].index;
        }
    }
    return -1;
}

/**
 * @brief 绘制 ASCII 字符串（自动换行）
 * @param page  起始页
 * @param col   起始列
 * @param str   字符串
 * @return 结束时的列坐标
 */
uint8_t SH1106_DrawStringWrap(uint8_t page, uint8_t col, const char* str)
{
    while (*str) {
        // 如果当前列超出屏幕，自动换行到下一页
        if (col + 8 > SH1106_WIDTH) {
            col = 0;
            page += 2;  // 8x16字体占2页
            if (page >= (SH1106_HEIGHT / 8))
                break;  // 超出屏幕，停止绘制
        }
        SH1106_DrawChar(page, col, *str);
        col += 9;  // 8像素宽 + 1像素间距
        str++;
    }
    return col;
}

/**
 * @brief 绘制 UTF-8 字符串（自动换行，支持中英文混排）
 * @param page  起始页
 * @param col   起始列
 * @param str   UTF-8 字符串
 * @return 结束时的列坐标
 */
uint8_t SH1106_DrawUTF8StringWrap(uint8_t page, uint8_t col, const char *str)
{
    const uint8_t *p = (const uint8_t *)str;
    uint8_t cur_col = col;

    while (*p) {
        // 解析 UTF-8 字符长度
        uint8_t len = 0;
        uint8_t char_width = 9;  // ASCII默认宽度
        if ((*p & 0x80) == 0) {
            len = 1;  // ASCII
        } else if ((*p & 0xE0) == 0xC0) {
            len = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            len = 3;  // 中文
            char_width = 17;  // 16像素宽 + 1像素间距
        } else if ((*p & 0xF8) == 0xF0) {
            len = 4;
        } else {
            p++;
            continue;
        }

        // 如果当前字符会导致换行
        if (cur_col + char_width > SH1106_WIDTH) {
            cur_col = 0;
            page += 2;  // 8x16字体占2页
            if (page >= (SH1106_HEIGHT / 8))
                break;  // 超出屏幕
        }

        if (len == 1) {
            // ASCII 字符
            SH1106_DrawChar(page, cur_col, (char)*p);
            cur_col += 9;
            p++;
        } else {
            // 中文汉字
            char tmp[5] = {0};
            memcpy(tmp, p, len);
            int16_t idx = find_chinese_index(tmp);
            if (idx >= 0) {
                SH1106_ShowChinese(page, cur_col, (uint16_t)idx);
                cur_col += 17;
            } else {
                // 未找到，显示空格
                SH1106_DrawChar(page, cur_col, ' ');
                cur_col += 9;
            }
            p += len;
        }
    }
    return cur_col;
}



/**
 * @brief 显示 UTF-8 字符串（中英文混排）
 * @param page  起始页
 * @param col   起始列
 * @param str   UTF-8 字符串
 * @return 结束时的列坐标，若超出屏幕则返回 SH1106_WIDTH
 */
uint8_t SH1106_DrawUTF8String(uint8_t page, uint8_t col, const char *str)
{
    const uint8_t *p = (const uint8_t *)str;
    uint8_t cur_col = col;

    while (*p && cur_col < SH1106_WIDTH) {
        // 解析 UTF-8 字符长度
        uint8_t len = 0;
        if ((*p & 0x80) == 0) {
            len = 1;  // ASCII
        } else if ((*p & 0xE0) == 0xC0) {
            len = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            len = 3;
        } else if ((*p & 0xF8) == 0xF0) {
            len = 4;
        } else {
            // 无效字节，跳过
            p++;
            continue;
        }

        if (len == 1) {
            // 英文字符：使用原有的 DrawChar 逻辑
            char tmp[2] = {*p, '\0'};
            SH1106_DrawString(page, cur_col, tmp);
            cur_col += 9;  // 8像素宽 + 1像素间距
            p++;
        } else {
            // 中文汉字
            char tmp[5] = {0};
            memcpy(tmp, p, len);
            int16_t idx = find_chinese_index(tmp);
            if (idx >= 0) {
                if (cur_col + 16 > SH1106_WIDTH) {
                    break;  // 剩余空间不够显示一个汉字
                }
                SH1106_ShowChinese(page, cur_col, (uint16_t)idx);
                cur_col += 16;
            } else {
                // 未在字库中找到，显示问号
                SH1106_DrawString(page, cur_col, "?");
                cur_col += 9;  // 8像素宽 + 1像素间距
            }
            p += len;
        }
    }
    return cur_col;
}



/**
 * @brief 在指定位置绘制一个 16x16 的汉字（适配阴码、列行式、逆向取模）
 * @param page  起始页（0~7），每页8像素
 * @param col   起始列（0~127）
 * @param index 汉字在 ChineseFont_16x16 中的索引
 */
void SH1106_ShowChinese(uint8_t page, uint8_t col, uint16_t index)
{
    if (index >= ChineseFont_Count)
        return;
    if (page >= (SH1106_HEIGHT / 8) || col >= SH1106_WIDTH)
        return;
    if (col + 16 > SH1106_WIDTH)
        return;

    const uint8_t *font = ChineseFont_16x16[index];

    // 16x16 汉字，列行式取模：每个字节是一列，共16列，每列8像素（上半页）
    // 前16字节 -> 第0-7行（占用page和page+1页的同一列位置）
    // 后16字节 -> 第8-15行（占用page+1和page+2页的同一列位置）
    for (uint8_t i = 0; i < 16; i++) {
        uint8_t upper = font[i];       // 上半字节
        uint8_t lower = font[i + 16]; // 下半字节

        uint16_t idx = page * SH1106_WIDTH + col + i;

        // 第一页：上半字节（bit7 对应 y=0）
        if (page < (SH1106_HEIGHT / 8)) {
            s_buffer[idx] = upper;
        }

        // 第二页：下半字节
        uint8_t page2 = page + 1;
        if (page2 < (SH1106_HEIGHT / 8)) {
            s_buffer[page2 * SH1106_WIDTH + col + i] = lower;
        }
    }
}

/**
 * @brief 显示一串中文（通过索引数组）
 * @param page    起始页
 * @param col     起始列
 * @param indices 索引数组
 * @param count   汉字个数
 */
void SH1106_ShowChineseString(uint8_t page, uint8_t col, const uint16_t *indices, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        SH1106_ShowChinese(page, col + i * 16, indices[i]);
    }
}