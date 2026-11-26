#include "stm32f10x.h"                   /* 使用 STM32F1 的寄存器、外设类型和基于工程配置启用的驱动接口。 */ // Device header
#include "delay.h" /* 使用 SysTick 毫秒计时和短时延时接口。 */
#include "Key.h" /* 取得按键 GPIO 初始化和按键状态读取接口。 */

/**
  * 函    数：按键初始化
  * 参    数：无
  * 返 回 值：无
  */
void Key_Init(void) /* 配置按键输入，使软件可以读取实际按键电平。 */
{
	/*开启时钟*/
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);		//开启GPIOC的时钟
	
	/*GPIO初始化*/
	GPIO_InitTypeDef GPIO_InitStructure; /* 声明GPIO 初始化 Structure，为后续计算或读取保存中间值。 */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; /* 把GPIO 输入或输出工作模式设为 上拉输入，由随后外设初始化统一写入。 */
	GPIO_InitStructure.GPIO_Pin = KEY1_GPIO_PIN | KEY2_GPIO_PIN; /* 把当前配置涉及的引脚掩码设为 「KEY1_GPIO_PIN | KEY2_GPIO_PIN」，由随后外设初始化统一写入。 */
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; /* 把GPIO 输出驱动的速度档位设为 「GPIO_Speed_50MHz」，由随后外设初始化统一写入。 */
	GPIO_Init(GPIOC, &GPIO_InitStructure);						//将PC2和PC3引脚初始化为上拉输入
}

/**
  * 函    数：按键获取键码
  * 参    数：无
  * 返 回 值：bit0/bit1 为 Key1/Key2 新按下，bit2/bit3 为两键长按，事件可组合。
  * 注意事项：主循环轮询时使用。原始电平变化后需保持 20 ms 才确认边沿，
  *           长按从确认按下时起算，每次按住最多上报一次，松开只复位内部状态。
  */
uint8_t Key_GetNum(void) /* 按键 读取 Num。 */
{
	uint8_t KeyNum = 0;		//定义变量，默认键码值为0
	uint32_t now = delay_millis(); /* 定义当前时间，把当前毫秒节拍保存到 now，作为之后计算时间差的基准。 */

	static uint8_t key1_last = 1; // 上拉输入，1为松开
	static uint8_t key1_stable = 1; /* 定义按键 1 stable，将按键 1 stable 更新为 「1」。 */
	static uint32_t key1_last_change = 0; /* 将按键 1 last change初始化为零，给模块一个确定的起始值。 */
	static uint32_t key1_press_start = 0;     // 按下开始时刻
	static uint8_t  key1_long_done = 0;       // 长按已触发标志

	static uint8_t key2_last = 1; /* 定义按键 2 last，将按键 2 last 更新为 「1」。 */
	static uint8_t key2_stable = 1; /* 定义按键 2 stable，将按键 2 stable 更新为 「1」。 */
	static uint32_t key2_last_change = 0; /* 将按键 2 last change初始化为零，给模块一个确定的起始值。 */
	static uint32_t key2_press_start = 0; /* 将按键 2 press 启动初始化为零，给模块一个确定的起始值。 */
	static uint8_t  key2_long_done = 0; /* 将按键 2 long 完成初始化为零，给模块一个确定的起始值。 */

	// Key1 (PC2)
	uint8_t sample1 = GPIO_ReadInputDataBit(KEY1_GPIO_PORT, KEY1_GPIO_PIN); /* 定义sample1，将sample1 更新为 「GPIO_ReadInputDataBit(KEY1_GPIO_PORT, KEY1_GPIO_PIN)」。 */
	if (sample1 != key1_last) /* 当 「sample1 != key1_last」 成立时处理下方分支。 */
	{
		key1_last = sample1; /* 将按键 1 last 更新为 「sample1」。 */
		key1_last_change = now; /* 将按键 1 last change 更新为 「now」。 */
	}
	if ((uint32_t)(now - key1_last_change) >= 20U) /* 当 「(uint32_t)(now - key1_last_change) >= 20U」 成立时处理下方分支。 */
	{
		if (key1_stable != key1_last) /* 当 「key1_stable != key1_last」 成立时处理下方分支。 */
		{
			key1_stable = key1_last; /* 将按键 1 stable 更新为 「key1_last」。 */
			if (key1_stable == 0U) /* 当 「key1_stable == 0U」 成立时处理下方分支。 */
			{
				KeyNum |= KEY1_FLAG;              // 按下
				key1_press_start = now; /* 将按键 1 press 启动 更新为 「now」。 */
				key1_long_done = 0; /* 将按键 1 long 完成 更新为 「0」。 */
			}
			else /* 前面的条件不满足时执行此分支。 */
			{
				key1_press_start = 0;              // 松开清除
				key1_long_done = 0; /* 将按键 1 long 完成 更新为 「0」。 */
			}
		}
	}
	// 长按判定：按住超过 2s 且未触发过，置位一次
	if (key1_stable == 0U && key1_long_done == 0U && /* 长按判定：按住超过 2s 且未触发过，置位一次。 */
	    (uint32_t)(now - key1_press_start) >= KEY_LONG_PRESS_MS) /* 用 「(uint32_t)(now - key1_press_start) >= KEY_LONG_PRESS_MS)」 补足上行的表达式、缓冲区内容或接口参数。 */
	{
		KeyNum |= KEY1_LONG_FLAG; /* 将按键 Num 按位置位 「KEY1_LONG_FLAG」。 */
		key1_long_done = 1; /* 将按键 1 long 完成 更新为 「1」。 */
	}

	// Key2 (PC3)
	uint8_t sample2 = GPIO_ReadInputDataBit(KEY2_GPIO_PORT, KEY2_GPIO_PIN); /* 定义sample2，将sample2 更新为 「GPIO_ReadInputDataBit(KEY2_GPIO_PORT, KEY2_GPIO_PIN)」。 */
	if (sample2 != key2_last) /* 当 「sample2 != key2_last」 成立时处理下方分支。 */
	{
		key2_last = sample2; /* 将按键 2 last 更新为 「sample2」。 */
		key2_last_change = now; /* 将按键 2 last change 更新为 「now」。 */
	}
	if ((uint32_t)(now - key2_last_change) >= 20U) /* 当 「(uint32_t)(now - key2_last_change) >= 20U」 成立时处理下方分支。 */
	{
		if (key2_stable != key2_last) /* 当 「key2_stable != key2_last」 成立时处理下方分支。 */
		{
			key2_stable = key2_last; /* 将按键 2 stable 更新为 「key2_last」。 */
			if (key2_stable == 0U) /* 当 「key2_stable == 0U」 成立时处理下方分支。 */
			{
				KeyNum |= KEY2_FLAG; /* 将按键 Num 按位置位 「KEY2_FLAG」。 */
				key2_press_start = now; /* 将按键 2 press 启动 更新为 「now」。 */
				key2_long_done = 0; /* 将按键 2 long 完成 更新为 「0」。 */
			}
			else /* 前面的条件不满足时执行此分支。 */
			{
				key2_press_start = 0; /* 将按键 2 press 启动 更新为 「0」。 */
				key2_long_done = 0; /* 将按键 2 long 完成 更新为 「0」。 */
			}
		}
	}
	if (key2_stable == 0U && key2_long_done == 0U && /* 检查 「key2_stable == 0U && key2_long_done == 0U &&」，其余条件接在后续行。 */
	    (uint32_t)(now - key2_press_start) >= KEY_LONG_PRESS_MS) /* 用 「(uint32_t)(now - key2_press_start) >= KEY_LONG_PRESS_MS)」 补足上行的表达式、缓冲区内容或接口参数。 */
	{
		KeyNum |= KEY2_LONG_FLAG; /* 将按键 Num 按位置位 「KEY2_LONG_FLAG」。 */
		key2_long_done = 1; /* 将按键 2 long 完成 更新为 「1」。 */
	}

	return KeyNum;			// 本次调用无新事件时返回 0
}
