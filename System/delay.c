#include "delay.h"                                                 // 延时接口声明
#include "sys.h"                                                   // 系统基础类型定义
#include "LED.h"                                                   // 工程通用头文件依赖
#include "GPIO.h"                                                  // 工程通用头文件依赖
#include "flag.h"                                                  // 工程通用头文件依赖
#include "stm32f10x.h"                                             // STM32寄存器与内核定义

static u32  fac_us=0;                                                 // us延时倍乘系数
static volatile u32 g_ms = 0;                                         // 1ms系统节拍计数值




// 初始化延时模块
void delay_init()	 
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

// us级阻塞延时
void delay_us(u32 nus)
{		
	u32 ticks = nus * fac_us;                                          // 计算本次延时需要等待的总周期数
	u32 start = DWT->CYCCNT;                                           // 记录当前周期计数起点
	while ((DWT->CYCCNT - start) < ticks)
	{
		// busy wait                                                     // 忙等待直到达到目标周期数
	}
}

// ms级阻塞延时
void delay_ms(u16 nms)
{
	u32 start = delay_millis();                                        // 记录当前ms节拍起点
	while (!delay_expired(start, nms))
	{
		// busy wait                                                     // 忙等待直到超时
	}
}

uint32_t delay_tick_inc(void)
{
	return ++g_ms;                                                     // SysTick中断中调用，节拍加1并返回最新值
}

uint32_t delay_millis(void)
{
	return g_ms;                                                       // 获取当前系统运行毫秒数
}

bool delay_expired(uint32_t start_ms, uint32_t timeout_ms)
{
	return (uint32_t)(delay_millis() - start_ms) >= timeout_ms;        // 通过节拍差判断是否已经超时
}





























