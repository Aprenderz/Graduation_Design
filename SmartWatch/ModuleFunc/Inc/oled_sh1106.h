
/*
 * oled_sh1106.h
 *
 * Minimal SH1106 (1.3") driver for 128x64 OLED displays on I2C.
 * Intended for use in a shared I2C bus environment.
 */

#ifndef __OLED_SH1106_H__
#define __OLED_SH1106_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

#define SH1106_WIDTH   128
#define SH1106_HEIGHT  64

/**
 * Initialize the display.
 *
 * @param hi2c HAL I2C handle (e.g. &hi2c1)
 * @return true if init succeeds
 */
bool SH1106_Init(I2C_HandleTypeDef* hi2c);

/**
 * Clear internal framebuffer (does not update the screen until SH1106_Update is called).
 */
void SH1106_Clear(void);

/**
 * Send framebuffer to the display.
 */
void SH1106_Update(void);

/**
 * Draw an ASCII string at given page/column.
 * Note: Uses 8x16 font (8 pixels wide, 16 pixels high = 2 pages)
 *
 * @param page  Page (0..6) - must leave room for 16px height (2 pages)
 * @param col   Column (0..127)
 * @param str   Null-terminated ASCII string
 */
void SH1106_DrawString(uint8_t page, uint8_t col, const char* str);

/**
 * @brief 在指定位置绘制一个 16x16 的汉字（适配阴码、列行式、逆向取模）
 * @param page  起始页（0~7），每页8像素
 * @param col   起始列（0~127）
 * @param index 汉字在 ChineseFont_16x16 中的索引
 */
void SH1106_ShowChinese(uint8_t page, uint8_t col, uint16_t index);

/**
 * @brief 显示一串中文（通过索引数组）
 * @param page    起始页
 * @param col     起始列
 * @param indices 索引数组
 * @param count   汉字个数
 */
void SH1106_ShowChineseString(uint8_t page, uint8_t col, const uint16_t *indices, uint8_t count);

/**
 * @brief 显示 UTF-8 字符串（中英文混排，自动使用内置中文字库）
 * @param page  起始页 (0~7)
 * @param col   起始列 (0~127)
 * @param str   UTF-8 编码的字符串
 * @return 返回结束时的列位置，若超出屏幕则返回 SH1106_WIDTH
 */
uint8_t SH1106_DrawUTF8String(uint8_t page, uint8_t col, const char *str);

/**
 * @brief 绘制 ASCII 字符串（自动换行）
 * @param page  起始页
 * @param col   起始列
 * @param str   字符串
 * @return 结束时的列坐标
 */
uint8_t SH1106_DrawStringWrap(uint8_t page, uint8_t col, const char* str);

/**
 * @brief 绘制 UTF-8 字符串（自动换行，支持中英文混排）
 * @param page  起始页
 * @param col   起始列
 * @param str   UTF-8 字符串
 * @return 结束时的列坐标
 */
uint8_t SH1106_DrawUTF8StringWrap(uint8_t page, uint8_t col, const char *str);

#ifdef __cplusplus
}
#endif

#endif // __OLED_SH1106_H__