#ifndef __LED_H
#define __LED_H

#define LED1_PIN    GPIO_Pin_4
#define LED1_PORT   GPIOA
#define LED2_PIN    GPIO_Pin_5
#define LED2_PORT   GPIOA



void LED_Init(void);
void LED1_ON(void);
void LED1_OFF(void);
void LED1_Turn(void);
void LED2_ON(void);
void LED2_OFF(void);
void LED2_Turn(void);
void LED_flicker(void);

#endif
