#ifndef __TW_TTS_H
#define __TW_TTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdarg.h>
#include <string.h>

/* 编码格式定义 */
#define TTS_ENCODE_GB2312   0x00
#define TTS_ENCODE_UTF8     0x04

/* 命令字定义 */
#define TTS_CMD_START       0x01    // 开始合成
#define TTS_CMD_STOP        0x02    // 停止合成
#define TTS_CMD_PAUSE       0x03    // 暂停合成
#define TTS_CMD_RESUME      0x04    // 继续合成
#define TTS_CMD_QUERY       0x21    // 查询状态（如需使用需自行处理回复）

/* 默认参数 */
#define TTS_DEFAULT_VOLUME  5       // 默认音量
#define TTS_DEFAULT_SPEED   5       // 默认语速
#define TTS_DEFAULT_TONE    5       // 默认语调

/**
 * @brief 初始化TTS模块（仅设置UART句柄）
 * @param huart 使用的UART句柄
 * @retval 0: 成功, -1: 失败
 */
int8_t TTS_Init(UART_HandleTypeDef *huart);

/**
 * @brief 播放文本
 * @param text 要播放的文本字符串
 * @param encode 编码格式 (TTS_ENCODE_GB2312 或 TTS_ENCODE_UTF8)
 * @retval 0: 成功, -1: 失败
 */
int8_t TTS_Play(const char *text, uint8_t encode);

/**
 * @brief 播放格式化文本（支持printf风格格式化）
 * @param encode 编码格式
 * @param format 格式化字符串
 * @param ... 可变参数
 * @retval 0: 成功, -1: 失败
 */
int8_t TTS_Printf(uint8_t encode, const char *format, ...);

/**
 * @brief 停止播放
 * @retval 0: 成功, -1: 失败
 */
int8_t TTS_Stop(void);

/**
 * @brief 暂停播放
 * @retval 0: 成功, -1: 失败
 */
int8_t TTS_Pause(void);

/**
 * @brief 继续播放
 * @retval 0: 成功, -1: 失败
 */
int8_t TTS_Resume(void);

/**
 * @brief 设置音量
 * @param volume 音量级别 0-9 (0:静音, 9:最大)
 * @retval 0: 成功, -1: 失败
 */
int8_t TTS_SetVolume(uint8_t volume);

/**
 * @brief 设置语速
 * @param speed 语速级别 0-9 (0:最快, 9:最慢)
 * @retval 0: 成功, -1: 失败
 */
int8_t TTS_SetSpeed(uint8_t speed);

/**
 * @brief 设置语调
 * @param tone 语调级别 0-9 (0:最低, 9:最高)
 * @retval 0: 成功, -1: 失败
 */
int8_t TTS_SetTone(uint8_t tone);

/**
 * @brief 播放提示音
 * @param index 提示音序号 1-5 (1为天问正品验证音效)
 * @retval 0: 成功, -1: 失败
 */
int8_t TTS_PlayMsg(uint8_t index);

/**
 * @brief 播放警示音
 * @param index 警示音序号 1-5
 * @retval 0: 成功, -1: 失败
 */
int8_t TTS_PlayAlert(uint8_t index);

/**
 * @brief 播放铃声
 * @param index 铃声序号 1-5
 * @retval 0: 成功, -1: 失败
 */
int8_t TTS_PlayRing(uint8_t index);

#ifdef __cplusplus
}
#endif

#endif /* __TW_TTS_H */