#ifndef __KEY_H
#define __KEY_H

#include "stm32f10x.h"

/* 按键引脚定义 */
#define KEY1_GPIO_PORT      GPIOC
#define KEY1_GPIO_PIN       GPIO_Pin_2
#define KEY2_GPIO_PORT      GPIOC
#define KEY2_GPIO_PIN       GPIO_Pin_3

/* 长按时间阈值 (ms) */
#define KEY_LONG_PRESS_MS   2000U

/* 返回值位定义 */
#define KEY1_FLAG           0x01U   // Key1 按下
#define KEY2_FLAG           0x02U   // Key2 按下
#define KEY1_LONG_FLAG      0x04U   // Key1 长按
#define KEY2_LONG_FLAG      0x08U   // Key2 长按

void Key_Init(void);
uint8_t Key_GetNum(void);

#endif
