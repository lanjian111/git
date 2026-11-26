/**
  ******************************************************************************
  * @file    Project/STM32F10x_StdPeriph_Template/stm32f10x_it.h 
  * @author  MCD Application Team
  * @version V3.5.0
  * @date    08-April-2011
  * @brief   This file contains the headers of the interrupt handlers.
  ******************************************************************************
  * @attention
  *
  * THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
  * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
  * TIME. AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY
  * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
  * FROM THE CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
  * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
  *
  * <h2><center>&copy; COPYRIGHT 2011 STMicroelectronics</center></h2>
  ******************************************************************************
  */ 

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __STM32F10x_IT_H /* 仅在尚未定义 __STM32F10x_IT_H时编译下方代码。 */
#define __STM32F10x_IT_H /* 定义 __STM32F10x_IT_H，供头文件防重包含或后续条件编译检查。 */

#ifdef __cplusplus /* 仅在已定义 __cplusplus时编译下方代码。 */
 extern "C" { /* 以 C 链接规则公开这些接口，便于 C++ 调用同一目标文件。 */
#endif  /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

/* Includes ------------------------------------------------------------------*/
#include "stm32f10x.h" /* 使用 STM32F1 的寄存器、外设类型和基于工程配置启用的驱动接口。 */

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/
/* Exported macro ------------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */

void NMI_Handler(void); /* 声明接口，NMI 处理入口。 */
void HardFault_Handler(void); /* 声明接口，接管内核硬故障，使设备停在故障入口便于调试。 */
void MemManage_Handler(void); /* 声明接口，接管内存管理异常，使设备停在故障入口便于调试。 */
void BusFault_Handler(void); /* 声明接口，接管总线访问异常，使设备停在故障入口便于调试。 */
void UsageFault_Handler(void); /* 声明接口，接管指令执行或状态异常，使设备停在故障入口便于调试。 */
void SVC_Handler(void); /* 声明接口，SVC 处理入口。 */
void DebugMon_Handler(void); /* 声明接口，Debug Mon 处理入口。 */
void PendSV_Handler(void); /* 声明接口，Pend SV 处理入口。 */
/* USART1 日志口，DMA1 Channel4 为 TX，Channel5 为 RX。 */
void DMA1_Channel4_IRQHandler(void); /* 声明接口，USART1 日志口，DMA1 Channel4 为 TX，Channel5 为 RX。 */
void DMA1_Channel5_IRQHandler(void); /* 声明接口，处理 DMA1_Channel5 的 DMA 中断，并转交对应串口处理。 */
/* 旧接口声明。此工程的屏幕使用 USART3，因此实际中断入口在 Channel2/3。 */
void DMA1_Channel7_IRQHandler(void); /* 声明接口，旧接口声明。此工程的屏幕使用 USART3，因此实际中断入口在 Channel2/3。 */
void DMA1_Channel6_IRQHandler(void); /* 声明接口，处理 DMA1_Channel6 的 DMA 中断，并转交对应串口处理。 */
/* 1 ms 时基中断，累计 100 次后置周期任务标志。 */
void SysTick_Handler(void); /* 声明接口，响应毫秒节拍中断，并生成供主循环消费的周期标志。 */

#ifdef __cplusplus /* 仅在已定义 __cplusplus时编译下方代码。 */
}
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

#endif /* __STM32F10x_IT_H */ /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
