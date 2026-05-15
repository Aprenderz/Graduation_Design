#include "tw_tts.h"
#include "main.h"
#include <stdio.h>

/* 内部变量 */
static UART_HandleTypeDef *pUartHandle = NULL;

/* 内部函数声明 */
static int8_t TTS_SendCommand(const uint8_t *cmd, uint16_t len);
static int8_t TTS_SendParamCommand(const char *param, uint8_t value);
static uint16_t TTS_BuildFrame(uint8_t *frame, const char *text, uint8_t encode);

/**
 * @brief 构建发送帧
 * @param frame 输出缓冲区
 * @param text 文本内容
 * @param encode 编码格式
 * @retval 帧长度
 */
static uint16_t TTS_BuildFrame(uint8_t *frame, const char *text, uint8_t encode)
{
    uint16_t text_len = strlen(text);
    uint16_t frame_len = 5 + text_len;  // 关键：5字节固定头 + 文本长度

    frame[0] = 0xFD;                                    // 帧头
    frame[1] = (uint8_t)((text_len + 2) >> 8);          // 数据长度高字节
    frame[2] = (uint8_t)((text_len + 2) & 0xFF);        // 数据长度低字节
    frame[3] = TTS_CMD_START;                           // 命令字
    frame[4] = encode;                                   // 编码参数
    memcpy(&frame[5], text, text_len);                   // 文本内容

    return frame_len;
}

/**
 * @brief 发送命令帧
 * @param cmd 命令帧数据
 * @param len 数据长度
 * @retval 0:成功, -1:失败
 */
static int8_t TTS_SendCommand(const uint8_t *cmd, uint16_t len)
{
    if (pUartHandle == NULL || cmd == NULL || len == 0) return -1;
    
    HAL_StatusTypeDef status = HAL_UART_Transmit(pUartHandle, (uint8_t*)cmd, len, 100);
    if (status != HAL_OK) return -1;
    return 0;
}

/**
 * @brief 发送参数调节命令（音量/语速/语调）
 * @param param 参数标识 (v/s/t)
 * @param value 参数值 0-9
 * @retval 0:成功, -1:失败
 */
static int8_t TTS_SendParamCommand(const char *param, uint8_t value)
{
    if (value > 9) value = 5;
    
    uint8_t frame[] = {0xFD, 0x00, 0x06, 0x01, 0x01, 0x5B, param[0], 0x30 + value, 0x5D};
    return TTS_SendCommand(frame, sizeof(frame));
}

/**
 * @brief 初始化TTS模块（仅设置UART句柄）
 * @param huart UART句柄
 * @retval 0:成功, -1:失败
 */
int8_t TTS_Init(UART_HandleTypeDef *huart)
{
    if (huart == NULL)  return -1;
    pUartHandle = huart;
    return 0;
}

/**
 * @brief 播放文本
 * @param text 文本内容
 * @param encode 编码格式
 * @retval 0:成功, -1:失败
 */
int8_t TTS_Play(const char *text, uint8_t encode)
{
    if (pUartHandle == NULL || text == NULL || strlen(text) == 0) return -1;
    
    uint8_t frame[256];
    uint16_t frame_len = TTS_BuildFrame(frame, text, encode);
    
    if (frame_len > sizeof(frame))  return -1;
    
    return TTS_SendCommand(frame, frame_len);
}

/**
 * @brief 格式化打印并播放
 * @param encode 编码格式
 * @param format 格式化字符串
 * @param ... 可变参数
 * @retval 0:成功, -1:失败
 */
int8_t TTS_Printf(uint8_t encode, const char *format, ...)
{
    char buffer[400];
    va_list args;
    
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    va_end(args);
    
    if (len <= 0 || len >= sizeof(buffer))  return -1;
    
    return TTS_Play(buffer, encode);
}

/**
 * @brief 停止播放
 * @retval 0:成功, -1:失败
 */
int8_t TTS_Stop(void)
{
    uint8_t cmd[] = {0xFD, 0x00, 0x01, TTS_CMD_STOP};
    return TTS_SendCommand(cmd, sizeof(cmd));
}

/**
 * @brief 暂停播放
 * @retval 0:成功, -1:失败
 */
int8_t TTS_Pause(void)
{
    uint8_t cmd[] = {0xFD, 0x00, 0x01, TTS_CMD_PAUSE};
    return TTS_SendCommand(cmd, sizeof(cmd));
}

/**
 * @brief 继续播放
 * @retval 0:成功, -1:失败
 */
int8_t TTS_Resume(void)
{
    uint8_t cmd[] = {0xFD, 0x00, 0x01, TTS_CMD_RESUME};
    return TTS_SendCommand(cmd, sizeof(cmd));
}

/**
 * @brief 设置音量
 * @param volume 0-9
 * @retval 0:成功, -1:失败
 */
int8_t TTS_SetVolume(uint8_t volume)
{
    if (volume > 9) volume = 5;
    return TTS_SendParamCommand("v", volume);
}

/**
 * @brief 设置语速
 * @param speed 0-9
 * @retval 0:成功, -1:失败
 */
int8_t TTS_SetSpeed(uint8_t speed)
{
    if (speed > 9) speed = 5;
    return TTS_SendParamCommand("s", speed);
}

/**
 * @brief 设置语调
 * @param tone 0-9
 * @retval 0:成功, -1:失败
 */
int8_t TTS_SetTone(uint8_t tone)
{
    if (tone > 9) tone = 5;
    return TTS_SendParamCommand("t", tone);
}

/**
 * @brief 播放提示音
 * @param index 1-5
 * @retval 0:成功, -1:失败
 */
int8_t TTS_PlayMsg(uint8_t index)
{
    if (index < 1 || index > 5) {
        index = 1;
    }
    
    uint8_t cmd[] = {0xFD, 0x00, 0x0B, 0x01, 0x01, 0x6D, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65, 0x5F, 0x30 + index};
    return TTS_SendCommand(cmd, sizeof(cmd));
}

/**
 * @brief 播放警示音
 * @param index 1-5
 * @retval 0:成功, -1:失败
 */
int8_t TTS_PlayAlert(uint8_t index)
{
    if (index < 1 || index > 5) {
        index = 1;
    }
    
    uint8_t cmd[] = {0xFD, 0x00, 0x09, 0x01, 0x01, 0x61, 0x6C, 0x65, 0x72, 0x74, 0x5F, 0x30 + index};
    return TTS_SendCommand(cmd, sizeof(cmd));
}

/**
 * @brief 播放铃声
 * @param index 1-5
 * @retval 0:成功, -1:失败
 */
int8_t TTS_PlayRing(uint8_t index)
{
    if (index < 1 || index > 5) {
        index = 1;
    }
    
    uint8_t cmd[] = {0xFD, 0x00, 0x08, 0x01, 0x01, 0x72, 0x69, 0x6E, 0x67, 0x5F, 0x30 + index};
    return TTS_SendCommand(cmd, sizeof(cmd));
}