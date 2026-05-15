#ifndef __SMS_H__
#define __SMS_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SMS_MAX_PHONE_NUM   20
#define SMS_MAX_TEXT_LEN    140   // UCS2 hex 最大长度限制（实际 70 个中文字符）

/**
 * @brief 初始化短信模块
 * @param recv_cb 收到新短信时的回调（可为 NULL）
 * @param sent_cb 短信发送完成后的回调（可为 NULL）
 */
void SMS_Init();

/**
 * @brief 发送短信（源文件 UTF-8，自动转 UCS2 十六进制）
 * @param phone 目标手机号（ASCII）
 * @param text  短信内容（UTF-8 编码）
 * @return true 发送成功，false 失败
 */
bool SMS_SendText(const char *phone, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* __SMS_H__ */