Graduation_Design
毕业设计

题目：居家老人日常活动异常监测报警系统
功能：

<img width="337" height="264" alt="image" src="https://github.com/user-attachments/assets/5f796e82-d8de-4a1a-8f80-e8b3a1ad34ea" />

<img width="624" height="322" alt="image" src="https://github.com/user-attachments/assets/05efe202-ce2d-4133-bc3e-598e1ecc73d8" />

模块选型：
STM32F411CEU6核心板、YED-M724、MAX30102、MPU6050、DS18B20、ATGM336H、TW-TTS、1.3'OLED、喇叭、手机震动马达、3*6*4.3规格的轻触直插按键、5V2.4A升压板、3.7V锂电池。
整体接线：
<img width="624" height="513" alt="image" src="https://github.com/user-attachments/assets/1f237d76-4bf1-4f7b-9662-cf2470c38c26" />
<img width="498" height="500" alt="image" src="https://github.com/user-attachments/assets/b2366cc0-f40d-418c-8186-a72e29c7fbdf" />
开发环境：
嵌入式端：VScode+STM32CubeMX，基于HAL库开发，启用freeRTOS实时操作系统；
小程序：微信开发者工具。

实物图：
<img width="521" height="457" alt="image" src="https://github.com/user-attachments/assets/8f145beb-ee50-4890-82f8-108a82dc8e33" />

测试：
    久坐监测功能默认处于关闭状态，如果需要该功能，直接长按功能键即可开启，功能关闭也是长按功能按键。功能开启后，系统持续监测使用者的静止时长，当静止时间超过预设阈值时，设备随即触发震动与语音提醒，并同时弹出提示弹窗。
<img width="422" height="148" alt="image" src="https://github.com/user-attachments/assets/3102e9ee-6bce-4b23-96fc-2288d2331c2c" />
<img width="276" height="146" alt="image" src="https://github.com/user-attachments/assets/080d34e9-be3a-4f68-82b5-7342f00df6bb" />

<img width="338" height="403" alt="image" src="https://github.com/user-attachments/assets/7fc56767-a34e-4084-a12d-3aee020bfeec" />

<img width="288" height="144" alt="image" src="https://github.com/user-attachments/assets/f1eee1a9-370b-4532-8645-73522604a058" />
<img width="946" height="655" alt="image" src="https://github.com/user-attachments/assets/a232fdab-5c19-423b-ac9c-4e7b3f68a980" />
<img width="933" height="662" alt="image" src="https://github.com/user-attachments/assets/0de3f20e-4538-4fa9-bec4-f271384789f3" />
<img width="931" height="658" alt="image" src="https://github.com/user-attachments/assets/957a1e48-cd2b-4092-af0e-867d4bc846a5" />
