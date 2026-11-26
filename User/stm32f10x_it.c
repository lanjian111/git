/**
  ******************************************************************************
  * @file    Project/STM32F10x_StdPeriph_Template/stm32f10x_it.c 
  * @author  MCD Application Team
  * @version V3.5.0
  * @date    08-April-2011
  * @brief   Main Interrupt Service Routines.
  *          This file provides template for all exceptions handler and 
  *          peripherals interrupt service routine.
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

/* Includes ------------------------------------------------------------------*/
#include "stm32f10x_it.h" /* 使中断服务函数的定义和工程声明保持一致。 */
#include "USARTDMA.h" /* 取得日志与触摸屏串口的 DMA 收发接口。 */
#include "flag.h" /* 取得采样、调试及流程启动标志的共享声明。 */
#include "delay.h" /* 使用 SysTick 毫秒计时和短时延时接口。 */

/** @addtogroup STM32F10x_StdPeriph_Template
  * @{
  */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/******************************************************************************/
/*            Cortex-M3 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
  * @brief  This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void) /* NMI 处理入口。 */
{
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void) /* 接管内核硬故障，使设备停在故障入口便于调试。 */
{
  /* Go to infinite loop when Hard Fault exception occurs */
  while (1) /* 保持在循环中，依靠内部退出条件或外部复位结束。 */
  {
  }
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void) /* 接管内存管理异常，使设备停在故障入口便于调试。 */
{
  /* Go to infinite loop when Memory Manage exception occurs */
  while (1) /* 保持在循环中，依靠内部退出条件或外部复位结束。 */
  {
  }
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void) /* 接管总线访问异常，使设备停在故障入口便于调试。 */
{
  /* Go to infinite loop when Bus Fault exception occurs */
  while (1) /* 保持在循环中，依靠内部退出条件或外部复位结束。 */
  {
  }
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void) /* 接管指令执行或状态异常，使设备停在故障入口便于调试。 */
{
  /* Go to infinite loop when Usage Fault exception occurs */
  while (1) /* 保持在循环中，依靠内部退出条件或外部复位结束。 */
  {
  }
}

/**
  * @brief  This function handles SVCall exception.
  * @param  None
  * @retval None
  */
void SVC_Handler(void) /* SVC 处理入口。 */
{
}

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void) /* Debug Mon 处理入口。 */
{
}

/**
  * @brief  This function handles PendSVC exception.
  * @param  None
  * @retval None
  */
void PendSV_Handler(void) /* Pend SV 处理入口。 */
{
}

/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */
// void SysTick_Handler(void)
// {
// }

/******************************************************************************/
/*                 STM32F10x Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f10x_xx.s).                                            */
/******************************************************************************/

/**
  * @brief  This function handles PPP interrupt request.
  * @param  None
  * @retval None
  */
/*void PPP_IRQHandler(void)
{
}*/

/**
  * @}
  */ 


/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
/**
  * @brief  DMA1 Channel2 处理屏幕 USART3 TX，PB10。USART2_DMA 是驱动的兼容命名。
  */
void DMA1_Channel2_IRQHandler(void) /* DMA1 Channel2 处理屏幕 USART3 TX，PB10。USART2_DMA 是驱动的兼容命名。 */
{
    USART2_DMA_TX_IRQHandler(); /* 执行 USART2_DMA_TX_IRQHandler 的串口 2 DMA 发送 中断 处理入口操作。 */
}

/**
  * @brief  DMA1 Channel3 处理屏幕 USART3 RX，PB11。帧解析在主循环进行。
  */
void DMA1_Channel3_IRQHandler(void) /* DMA1 Channel3 处理屏幕 USART3 RX，PB11。帧解析在主循环进行。 */
{
    USART2_DMA_RX_IRQHandler(); /* 执行 USART2_DMA_RX_IRQHandler 的串口 2 DMA 接收 中断 处理入口操作。 */
}

/**
  * @brief  DMA1 Channel4中断处理（USART1 TX — 日志）
  */
void DMA1_Channel4_IRQHandler(void) /* DMA1 Channel4中断处理（USART1 TX — 日志）。 */
{
    USART_DMA_TX_IRQHandler(); /* 执行 USART_DMA_TX_IRQHandler 的串口 DMA 发送 中断 处理入口操作。 */
}

/**
  * @brief  DMA1 Channel5中断处理（USART1 RX — 日志）
  */
void DMA1_Channel5_IRQHandler(void) /* DMA1 Channel5中断处理（USART1 RX — 日志）。 */
{
    USART_DMA_RX_IRQHandler(); /* 执行 USART_DMA_RX_IRQHandler 的串口 DMA 接收 中断 处理入口操作。 */
}

/* 1 ms 中断只维护节拍和 100 ms 待处理标志，耗时任务由主循环执行。
 * FLAG_100MS 仅表示存在待处理周期，重复置位不会累积次数。 */
void SysTick_Handler(void) /* 响应毫秒节拍中断，并生成供主循环消费的周期标志。 */
{
    static uint8_t tick_cnt = 0; /* 将计时基准 cnt初始化为零，给模块一个确定的起始值。 */
    delay_tick_inc(); /* 增加系统毫秒计数并返回更新值，供 SysTick 处理使用。 */
    if (++tick_cnt >= 100U) /* 当 「++tick_cnt >= 100U」 成立时处理下方分支。 */
    {
        tick_cnt = 0; /* 将计时基准 cnt清零，使本轮从初始计数开始。 */
        FLAG_100MS = 1; /* 置位待消费的 100 ms 调度标志，供后续任务判断当前状态。 */
    }
}
