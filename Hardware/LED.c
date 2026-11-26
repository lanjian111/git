#include "stm32f10x.h"                   /* 使用 STM32F1 的寄存器、外设类型和基于工程配置启用的驱动接口。 */ // Device header
#include "LED.h" /* 取得指示灯初始化及状态控制接口。 */
#include "delay.h" /* 使用 SysTick 毫秒计时和短时延时接口。 */
#include "flag.h" /* 取得采样、调试及流程启动标志的共享声明。 */
/**
  * 函    数：LED初始化
  * 参    数：无
  * 返 回 值：无
  */
void LED_Init(void) /* 配置状态指示灯的 GPIO，供后续开关和闪烁任务控制。 */
{
	/*开启时钟*/
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);		//开启GPIOA的时钟
	
	/*GPIO初始化*/
	GPIO_InitTypeDef GPIO_InitStructure; /* 声明GPIO 初始化 Structure，为后续计算或读取保存中间值。 */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; /* 把GPIO 输入或输出工作模式设为 推挽输出，由随后外设初始化统一写入。 */
	GPIO_InitStructure.GPIO_Pin = LED1_PIN | LED2_PIN; /* 把当前配置涉及的引脚掩码设为 「LED1_PIN | LED2_PIN」，由随后外设初始化统一写入。 */
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; /* 把GPIO 输出驱动的速度档位设为 「GPIO_Speed_50MHz」，由随后外设初始化统一写入。 */
	GPIO_Init(GPIOA, &GPIO_InitStructure);						//将PA4和PA5引脚初始化为推挽输出
	
	/*设置GPIO初始化后的默认电平*/
	GPIO_SetBits(GPIOA, LED1_PIN | LED2_PIN);				//设置PA4和PA5引脚为高电平，默认关闭LED
}

/**
  * 函    数：LED1开启
  * 参    数：无
  * 返 回 值：无
  */
void LED1_ON(void) /* 指示灯 1 ON。 */
{
	GPIO_ResetBits(LED1_PORT, LED1_PIN);		//设置PA4引脚为低电平
}

/**
  * 函    数：LED1关闭
  * 参    数：无
  * 返 回 值：无
  */
void LED1_OFF(void) /* 指示灯 1 OFF。 */
{
	GPIO_SetBits(LED1_PORT, LED1_PIN);		//设置PA4引脚为高电平
}

/**
  * 函    数：LED1状态翻转
  * 参    数：无
  * 返 回 值：无
  */
void LED1_Turn(void) /* 指示灯 1 Turn。 */
{
	if (GPIO_ReadOutputDataBit(LED1_PORT, LED1_PIN) == 0)		//获取输出寄存器的状态，如果当前引脚输出低电平
	{
		GPIO_SetBits(LED1_PORT, LED1_PIN);					//则设置PA4引脚为高电平
	}
	else													//否则，即当前引脚输出高电平
	{
		GPIO_ResetBits(LED1_PORT, LED1_PIN);					//则设置PA4引脚为低电平
	}
}

/**
  * 函    数：LED2开启
  * 参    数：无
  * 返 回 值：无
  */
void LED2_ON(void) /* 指示灯 2 ON。 */
{
	GPIO_ResetBits(LED2_PORT, LED2_PIN);		//设置PA5引脚为低电平
}

/**
  * 函    数：LED2关闭
  * 参    数：无
  * 返 回 值：无
  */
void LED2_OFF(void) /* 指示灯 2 OFF。 */
{
	GPIO_SetBits(LED2_PORT, LED2_PIN);		//设置PA5引脚为高电平
}

/**
  * 函    数：LED2状态翻转
  * 参    数：无
  * 返 回 值：无
  */
void LED2_Turn(void) /* 指示灯 2 Turn。 */
{
	if (GPIO_ReadOutputDataBit(LED2_PORT, LED2_PIN) == 0)		//获取输出寄存器的状态，如果当前引脚输出低电平
	{                                                  
		GPIO_SetBits(LED2_PORT, LED2_PIN);               		//则设置PA5引脚为高电平
	}                                                  
	else                                               		//否则，即当前引脚输出高电平
	{                                                  
		GPIO_ResetBits(LED2_PORT, LED2_PIN);             		//则设置PA5引脚为低电平
	}
}


/* 由 100 ms 周期任务驱动，两个模式共享上次翻转时间。
 * 注释中的间隔是电平翻转间隔，完整亮灭周期是翻转间隔的两倍。 */
void LED_flicker(void) /* 推进状态灯闪烁，用于显示周期运行和调试状态。 */
{
  static uint32_t last_ms = 0; /* 将last 毫秒初始化为零，给模块一个确定的起始值。 */

  if (FLAG_DEBUG_MODE) /* 当 「FLAG_DEBUG_MODE」 成立时处理下方分支。 */
  {
    /* 调试模式每 200 ms 翻转 LED2。 */
    if (delay_expired(last_ms, 200U)) /* 以 last_ms 为起点，比较经过时间是否达到 200U。 */
    {
      LED1_OFF();                        // LED1 常灭
      LED2_Turn();                       // LED2 快闪
      last_ms = delay_millis(); /* 把当前毫秒节拍保存到 last_ms，作为之后计算时间差的基准。 */
    }
  }
  else /* 前面的条件不满足时执行此分支。 */
  {
    /* 正常模式每 1 s 翻转 LED1。 */
    if (delay_expired(last_ms, 1000U)) /* 以 last_ms 为起点，比较经过时间是否达到 1000U。 */
    {
      LED2_OFF();                        // LED2 常灭
      LED1_Turn();                       // LED1 慢闪
      last_ms = delay_millis(); /* 把当前毫秒节拍保存到 last_ms，作为之后计算时间差的基准。 */
    }
  }
}




