#include "sms.h"
#include "at_driver.h"
#include "cmsis_os2.h"
#include "main.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ==================== UTF-8 转 UCS2 十六进制（用于发送）====================
static int utf8_to_ucs2_bin(const char *utf8, uint8_t *bin_buf, int buf_size)
{
    int out_len = 0;
    while (*utf8 && (out_len + 1) < buf_size) {
        uint32_t cp = 0;
        uint8_t c = (uint8_t)*utf8;

        if (c < 0x80) {
            cp = c;
            utf8 += 1;
        } else if ((c & 0xE0) == 0xC0) {
            if (utf8[1] == 0) break;
            cp = ((c & 0x1F) << 6) | (utf8[1] & 0x3F);
            utf8 += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (utf8[1] == 0 || utf8[2] == 0) break;
            cp = ((c & 0x0F) << 12) | ((utf8[1] & 0x3F) << 6) | (utf8[2] & 0x3F);
            utf8 += 3;
        } else {
            break; // 不支持的 4 字节 UTF-8
        }

        if (cp > 0xFFFF) cp = 0x3F; // 超范围用 '?'

        bin_buf[out_len++] = (uint8_t)((cp >> 8) & 0xFF);  // 高字节
        bin_buf[out_len++] = (uint8_t)(cp & 0xFF);         // 低字节
    }
    return out_len;
}

// ==================== 静态变量 ====================
// static osSemaphoreId_t         s_prompt_sem = NULL;
static osSemaphoreId_t         s_send_result_sem = NULL;

// ==================== URC 回调 ====================

// 发送结果处理
static void on_send_result(const char *line)
{
    if (strstr(line, "+CMGS") != NULL)
        if (s_send_result_sem) osSemaphoreRelease(s_send_result_sem);
}

// ==================== 公共接口 ====================
void SMS_Init()
{
    // 创建信号量
    // s_prompt_sem = osSemaphoreNew(1, 0, NULL);
    s_send_result_sem = osSemaphoreNew(1, 0, NULL);

    // 注册 URC 回调
    // AT_RegisterURC("+CMTI:", on_new_sms_index);
    AT_RegisterURC("+CMGS:", on_send_result);
    // AT_RegisterURC(">", on_prompt);

    // 强制退出可能残留的数据模式（例如上次发送未完成）
    uint8_t esc = 0x1B;
    AT_SendRaw(&esc, 1, 100);
    osDelay(500);
    AT_ClearRespBuffer();   // 清空残留响应，避免干扰后续命令

    // 初始化短信服务（严格按照步骤）
    AT_SendCommand("AT+CSMS=1", 5000);          // 设置短信服务
    AT_SendCommand("AT+CMGF=1", 5000);          // TEXT 模式
    AT_SendCommand("AT+CSMP=17,167,0,8", 5000); // UCS2 编码（支持中文）

}

bool SMS_SendText(const char *phone, const char *text)
{
    if (!phone || !text) return false;
    if (strlen(phone) > SMS_MAX_PHONE_NUM) return false;

    // 1. UTF-8 → UCS2 二进制
    uint8_t ucs2_bin[SMS_MAX_TEXT_LEN * 2 + 2];
    int bin_len = utf8_to_ucs2_bin(text, ucs2_bin, sizeof(ucs2_bin));
    if (bin_len == 0)   return false;

    // 2. 清空信号量
    // osSemaphoreAcquire(s_prompt_sem, 0);
    osSemaphoreAcquire(s_send_result_sem, 0);

    // 3. 发送 AT+CMGS="号码"
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"\r\n", phone);
    AT_SendRaw((uint8_t*)cmd, strlen(cmd), 100);

    osDelay(200);
    // 5. 发送二进制 UCS2 内容
    AT_SendRaw(ucs2_bin, bin_len, 1000);

    // 6. 发送 Ctrl+Z (0x1A)
    uint8_t ctrl_z = 0x1A;
    AT_SendRaw(&ctrl_z, 1, 100);

    // 7. 等待发送结果
    if (osSemaphoreAcquire(s_send_result_sem, 15000) != osOK) return false;
    return true;
}