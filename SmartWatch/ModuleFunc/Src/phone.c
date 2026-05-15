#include "phone.h"
#include "sms.h"
#include "at_driver.h"
#include <string.h>
#include <stdio.h>

#define PHONE_CONNECT_TIMEOUT_MS  30000   // 30 秒

osSemaphoreId_t s_call_connect_sem = NULL;  // 电话接通信号量

void Phone_Init(void)
{
    // 直接开启 VOLTE（Air724UG 必须）
    AT_SendCommand("AT+SETVOLTE=1", 5000);
    s_call_connect_sem = osSemaphoreNew(1, 0, NULL);
}
bool Phone_Dial(const char *number)
{
    if (!number || strlen(number) == 0) return false;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "ATD%s;\r\n", number);

    if (!AT_SendRaw((uint8_t *)cmd, strlen(cmd), 1000)) {
        return false;
    }
    osDelay(50);
    AT_ClearRespBuffer();

    if (!s_call_connect_sem) return false;
    osSemaphoreAcquire(s_call_connect_sem, 0);  // 清空残留
    return (osSemaphoreAcquire(s_call_connect_sem, pdMS_TO_TICKS(PHONE_CONNECT_TIMEOUT_MS)) == osOK);
}

bool Phone_HangUp(void)
{
    return AT_SendCommand("AT+CHUP", 2000);
}

// ==================== UTF-8 转 UCS2 十六进制（用于发送）====================
static int utf8_to_ucs2_hex(const char *utf8, char *hex, int max_hex_len)
{
    int hex_len = 0;
    while (*utf8 && hex_len + 4 < max_hex_len) {
        uint32_t codepoint = 0;
        uint8_t c = (uint8_t)*utf8;

        if (c < 0x80) {
            codepoint = c;
            utf8 += 1;
        } else if ((c & 0xE0) == 0xC0) {
            if (utf8[1] == 0) break;
            codepoint = ((c & 0x1F) << 6) | (utf8[1] & 0x3F);
            utf8 += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (utf8[1] == 0 || utf8[2] == 0) break;
            codepoint = ((c & 0x0F) << 12) | ((utf8[1] & 0x3F) << 6) | (utf8[2] & 0x3F);
            utf8 += 3;
        } else {
            break; // 不支持的 4 字节 UTF-8
        }

        if (codepoint > 0xFFFF) codepoint = 0x3F; // 超出 UCS2 范围，替换为 '?'

        snprintf(hex + hex_len, 5, "%04X", (unsigned int)codepoint);
        hex_len += 4;
    }
    hex[hex_len] = '\0';
    return hex_len;
}
 

bool Phone_PlayTTS(const char *text)
{
    if (!text || strlen(text) == 0) return false;

    char ucs2_hex[512];
    int len = utf8_to_ucs2_hex(text, ucs2_hex, sizeof(ucs2_hex));
    if (len == 0) return false;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "AT+CTTS=1,\"%s\"", ucs2_hex);
    return AT_SendCommand(cmd, 5000);
}

/**
 * @brief 执行一次完整的 SOS 电话流程（阻塞式）
 * @param sensor 当前传感器数据（用于获取经纬度）
 */
void Decision_ExecuteCall(const char *tts_text, uint32_t timeout_ms)
{
    const char *phone_to_use = NULL;
    char phone[20];

    /* 1. 确定电话号码 */
    if (g_FamilyConfig.valid && strlen(g_FamilyConfig.phone) > 0) {
        phone_to_use = g_FamilyConfig.phone;
    } else {
        phone_to_use = DEFAULT_EMERGENCY_PHONE;
    }

    if (phone_to_use == NULL || strlen(phone_to_use) == 0) return;
    strcpy(phone, phone_to_use);

    /* 2. 暂停通信任务 */
    osSemaphoreAcquire(xCommPauseSemHandle, osWaitForever);

    bool call_ok = false;

    /* 3. 拨号尝试 */
    for (int attempt = 0; attempt < MAX_DIAL_ATTEMPTS; attempt++) {
        if (Phone_Dial(phone)) {
            call_ok = true;

            /* 4. 播放传入的 TTS 文本 */
            if (tts_text && Phone_PlayTTS(tts_text)) osDelay(timeout_ms);

            Phone_HangUp();
            break;
        } else {
            Phone_HangUp();
            osDelay(2000);
        }
    }

    /* 5. 电话失败则发短信 */
    if (!call_ok) {
        SMS_SendText(phone, tts_text);   // 删除原有的成功打印
    }

    /* 6. 恢复通信任务 */
    osSemaphoreRelease(xCommPauseSemHandle);
}

bool Phone_StopTTS(void)
{
    return AT_SendCommand("AT+CTTS=0", 2000);
}

bool Phone_SetTTSParams(uint8_t volume, uint8_t speed, uint8_t pitch)
{
    char cmd[64];
    // 参数：音量, 模式(0自动读数字), 音调, 语速
    snprintf(cmd, sizeof(cmd), "AT+CTTSPARAM=%d,0,%d,%d", volume, pitch, speed);
    if (AT_SendCommand(cmd, 2000)) return true;
    return false;
}