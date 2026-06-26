#include "stm32f10x.h"                  // Device header
#include "delay.h"
#include "Key.h"

/**
  * 函    数：按键初始化
  * 参    数：无
  * 返 回 值：无
  */
void Key_Init(void)
{
	/*开启时钟*/
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);		//开启GPIOC的时钟
	
	/*GPIO初始化*/
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = KEY1_GPIO_PIN | KEY2_GPIO_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOC, &GPIO_InitStructure);						//将PC2和PC3引脚初始化为上拉输入
}

/**
  * 函    数：按键获取键码
  * 参    数：无
  * 返 回 值：按下的按键键码值，bit0=Key1, bit1=Key2，可同时返回，0代表没有按键按下
  * 注意事项：非阻塞式，每调用一次返回当前是否有新的按键事件
  */
uint8_t Key_GetNum(void)
{
	uint8_t KeyNum = 0;		//定义变量，默认键码值为0
	uint32_t now = delay_millis();

	static uint8_t key1_last = 1; // 上拉输入，1为松开
	static uint8_t key1_stable = 1;
	static uint32_t key1_last_change = 0;
graph LR
    subgraph STM32F103RC
        PA0[PA0 ADC1_CH0<br/>液位传感器]
        PA4[PA4 LED1]
        PA5[PA5 LED2]
        PA6[PA6 循环泵]
        PA7[PA7 进水电磁阀]
        PA8[PA8 清洗电磁阀]
        PA9[PA9 USART1 TX<br/>调试串口]
        PA10[PA10 USART1 RX]
        PB1[PB1 按键1]
        PB10[PB10 USART3 TX<br/>HMI触摸屏]
        PB11[PB11 USART3 RX]
        PC4[PC4 循环三通阀]
        PC6[PC6 出液三通阀]
    end	static uint32_t key1_press_start = 0;     // 按下开始时刻
	static uint8_t  key1_long_done = 0;       // 长按已触发标志

	static uint8_t key2_last = 1;
	static uint8_t key2_stable = 1;
	static uint32_t key2_last_change = 0;
	static uint32_t key2_press_start = 0;
	static uint8_t  key2_long_done = 0;

	// Key1 (PC2)
	uint8_t sample1 = GPIO_ReadInputDataBit(KEY1_GPIO_PORT, KEY1_GPIO_PIN);
	if (sample1 != key1_last)
	{
		key1_last = sample1;
		key1_last_change = now;
	}
	if ((uint32_t)(now - key1_last_change) >= 20U)
	{
		if (key1_stable != key1_last)
		{
			key1_stable = key1_last;
			if (key1_stable == 0U)
			{
				KeyNum |= KEY1_FLAG;              // 按下
				key1_press_start = now;
				key1_long_done = 0;
			}
			else
			{
				key1_press_start = 0;              // 松开清除
				key1_long_done = 0;
			}
		}
	}
	// 长按判定：按住超过 2s 且未触发过，置位一次
	if (key1_stable == 0U && key1_long_done == 0U &&
	    (uint32_t)(now - key1_press_start) >= KEY_LONG_PRESS_MS)
	{
		KeyNum |= KEY1_LONG_FLAG;
		key1_long_done = 1;
	}

	// Key2 (PC3)
	uint8_t sample2 = GPIO_ReadInputDataBit(KEY2_GPIO_PORT, KEY2_GPIO_PIN);
	if (sample2 != key2_last)
	{
		key2_last = sample2;
		key2_last_change = now;
	}
	if ((uint32_t)(now - key2_last_change) >= 20U)
	{
		if (key2_stable != key2_last)
		{
			key2_stable = key2_last;
			if (key2_stable == 0U)
			{
				KeyNum |= KEY2_FLAG;
				key2_press_start = now;
				key2_long_done = 0;
			}
			else
			{
				key2_press_start = 0;
				key2_long_done = 0;
			}
		}
	}
	if (key2_stable == 0U && key2_long_done == 0U &&
	    (uint32_t)(now - key2_press_start) >= KEY_LONG_PRESS_MS)
	{
		KeyNum |= KEY2_LONG_FLAG;
		key2_long_done = 1;
	}

	return KeyNum;			//非阻塞返回：bit0=Key1, bit1=Key2, 可同时返回
}
