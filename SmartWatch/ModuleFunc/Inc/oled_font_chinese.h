/*
 * oled_font_chinese.h
 * 16x16 中文字库接口（59个汉字）
 */

#ifndef __OLED_FONT_CHINESE_H__
#define __OLED_FONT_CHINESE_H__

#include <stdint.h>

#define CHINESE_FONT_BYTES  32   // 每个16x16汉字占32字节

extern const uint8_t ChineseFont_16x16[][CHINESE_FONT_BYTES];
extern const uint16_t ChineseFont_Count;

#endif /* __OLED_FONT_CHINESE_H__ */