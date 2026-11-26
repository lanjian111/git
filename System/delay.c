#include "delay.h"                                                 // 延时接口声明
#include "sys.h"                                                   // 系统基础类型定义
#include "LED.h" /* 取得指示灯初始化及状态控制接口。 */
#include "GPIO.h" /* 取得板级泵阀控制和液位采样接口。 */
#include "flag.h" /* 取得采样、调试及流程启动标志的共享声明。 */
#include "stm32f10x.h"                                             // STM32寄存器与内核定义

static u32  fac_us=0;                                                 // us延时倍乘系数
static volatile u32 g_ms = 0;                                         // 1ms系统节拍计数值




/* SysTick 提供 1 ms 调度时间基准，DWT CYCCNT 则供软件 I2C 做微秒级忙等。
 * 两个计数都使用当前 SystemCoreClock 配置，上层用无符号计数差比较经过时间。
 */
void delay_init()	  /* 配置 1 ms SysTick 和 DWT 周期计数器，为任务计时及 I2C 延时建立基准。 */
{
	SysTick_Config(SystemCoreClock / 1000U);                           // 配置SysTick每1ms产生一次中断

	if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U)         // 如果DWT追踪功能未开启则先打开
	{
		CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;                 // 允许使用DWT周期计数器
	}
	DWT->CYCCNT = 0U;                                                  // 清零DWT周期计数器
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;                               // 开启DWT周期计数功能

	fac_us = SystemCoreClock / 1000000U;                               // 计算每1us对应的CPU时钟周期数
}										    

/* 微秒级忙等。nus * fac_us 必须能放入 u32，适合短总线时序。 */
void delay_us(u32 nus) /* 按 DWT 周期忙等所需的微秒数，以提供短总线时序。 */
{		
	u32 ticks = nus * fac_us;                                          // 计算本次延时需要等待的总周期数
	u32 start = DWT->CYCCNT;                                           // 记录当前周期计数起点
	while ((DWT->CYCCNT - start) < ticks) /* 在 「(DWT->CYCCNT - start) < ticks」 成立期间重复处理循环体。 */
	{
		// busy wait                                                     // 忙等待直到达到目标周期数
	}
}

/* 毫秒级忙等依赖 SysTick 中断继续运行，适用于允许中断抢占的调用环境。 */
void delay_ms(u16 nms) /* 基于 SysTick 毫秒节拍忙等指定时长。 */
{
	u32 start = delay_millis();                                        // 记录当前ms节拍起点
	while (!delay_expired(start, nms)) /* 在 「!delay_expired(start, nms)」 成立期间重复处理循环体。 */
	{
		// busy wait                                                     // 忙等待直到超时
	}
}

uint32_t delay_tick_inc(void) /* 增加系统毫秒计数并返回更新值，供 SysTick 处理使用。 */
{
	return ++g_ms;                                                     // SysTick中断中调用，节拍加1并返回最新值
}

uint32_t delay_millis(void) /* 读取系统累计运行毫秒数，供超时和周期任务计算时间差。 */
{
	return g_ms;                                                       // 获取当前系统运行毫秒数
}

bool delay_expired(uint32_t start_ms, uint32_t timeout_ms) /* 用无符号节拍差判断是否到期，使短于计数周期的等待可跨回卷。 */
{
	return (uint32_t)(delay_millis() - start_ms) >= timeout_ms;        // 通过节拍差判断是否已经超时
}





























