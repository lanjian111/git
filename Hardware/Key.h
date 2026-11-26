#ifndef __KEY_H /* 仅在尚未定义 __KEY_H时编译下方代码。 */
#define __KEY_H /* 定义 __KEY_H，供头文件防重包含或后续条件编译检查。 */

#include "stm32f10x.h" /* 使用 STM32F1 的寄存器、外设类型和基于工程配置启用的驱动接口。 */

/* 按键引脚定义 */
#define KEY1_GPIO_PORT      GPIOC /* 将按键 1 GPIO PORT绑定到 GPIOC，使板级配置和读写引用同一引脚定义。 */
#define KEY1_GPIO_PIN       GPIO_Pin_2 /* 将按键 1 GPIO PIN绑定到 GPIO_Pin_2，使板级配置和读写引用同一引脚定义。 */
#define KEY2_GPIO_PORT      GPIOC /* 将按键 2 GPIO PORT绑定到 GPIOC，使板级配置和读写引用同一引脚定义。 */
#define KEY2_GPIO_PIN       GPIO_Pin_3 /* 将按键 2 GPIO PIN绑定到 GPIO_Pin_3，使板级配置和读写引用同一引脚定义。 */

/* 长按时间阈值 (ms) */
#define KEY_LONG_PRESS_MS   2000U /* 将按键 LONG PRESS 毫秒设为 2000U 毫秒，供时间差比较统一使用。 */

/* 返回值位定义 */
#define KEY1_FLAG           0x01U   // Key1 按下
#define KEY2_FLAG           0x02U   // Key2 按下
#define KEY1_LONG_FLAG      0x04U   // Key1 长按
#define KEY2_LONG_FLAG      0x08U   // Key2 长按

/* 将 PC2/PC3 配为上拉输入，按下为低电平。 */
void Key_Init(void); /* 声明接口，配置按键输入，使软件可以读取实际按键电平。 */
/* 轮询并返回本次新产生的事件位。当前主循环只初始化按键 GPIO。 */
uint8_t Key_GetNum(void); /* 声明接口，轮询并返回本次新产生的事件位。当前主循环只初始化按键 GPIO。 */

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
