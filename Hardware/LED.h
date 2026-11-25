#ifndef __LED_H
#define __LED_H

#define LED1_PIN    GPIO_Pin_4
#define LED1_PORT   GPIOA
#define LED2_PIN    GPIO_Pin_5
#define LED2_PORT   GPIOA



/* 两路 LED 均为低电平点亮，初始化后关闭。Turn 翻转对应 GPIO 输出电平。 */
void LED_Init(void);
void LED1_ON(void);
void LED1_OFF(void);
void LED1_Turn(void);
void LED2_ON(void);
void LED2_OFF(void);
void LED2_Turn(void);
/* 由 100 ms 周期任务调用，根据 FLAG_DEBUG_MODE 选择闪烁的 LED 与间隔。 */
void LED_flicker(void);

#endif
