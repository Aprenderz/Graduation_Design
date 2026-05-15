#ifndef __PHONE_H__
#define __PHONE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "main.h"

#define MAX_DIAL_ATTEMPTS     2
#define DEFAULT_EMERGENCY_PHONE  "17366912931"

/**
 * @brief 初始化电话模块（自动开启 VOLTE 并设置必要参数）
 */
void Phone_Init(void);

/**
 * @brief 拨打电话
 * @param number 电话号码（如 "10086" 或 "+8613800138000"）
 * @return true 命令发送成功（不代表已接通）
 */
bool Phone_Dial(const char *number);

/**
 * @brief 挂断当前通话（所有通话）
 * @return true 成功
 */
bool Phone_HangUp(void);

/**
 * @brief 在通话中播放 TTS 语音给对方（需先接通）
 * @param text 文本内容，支持中文、数字、字母（推荐使用 GBK 编码的中文或 ASCII）
 * @param use_ucs2 true=使用 UCS2 编码（需将中文转为十六进制），false=使用 GBK/ASCII（中文可直接输入）
 * @return true 播放命令发送成功（不代表播放完成）
 */
bool Phone_PlayTTS(const char *text);

/**
 * @brief 执行一次 SOS 电话/短信报警流程（阻塞式）
 * @param tts_text 电话接通后播放的 TTS 文本（调用者负责构造）
 */
void Decision_ExecuteCall(const char *tts_text, uint32_t timeout_ms);

/**
 * @brief 停止 TTS 播放（如果有正在播放的）
 * @return true 成功
 */
bool Phone_StopTTS(void);

/**
 * @brief 设置 TTS 参数（音量、语速、音调）
 * @param volume 音量 0-100，默认50
 * @param speed 语速 1-100，默认50
 * @param pitch 音调 1-100，默认50
 * @return true 成功
 */
bool Phone_SetTTSParams(uint8_t volume, uint8_t speed, uint8_t pitch);

#ifdef __cplusplus
}
#endif

#endif /* __PHONE_H__ */