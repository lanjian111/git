/**
  ******************************************************************************
  * @file    Project/STM32F10x_StdPeriph_Template/stm32f10x_conf.h 
  * @author  MCD Application Team
  * @version V3.5.0
  * @date    08-April-2011
  * @brief   Library configuration file.
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
#ifndef __STM32F10x_CONF_H /* 仅在尚未定义 __STM32F10x_CONF_H时编译下方代码。 */
#define __STM32F10x_CONF_H /* 定义 __STM32F10x_CONF_H，供头文件防重包含或后续条件编译检查。 */

/* Includes ------------------------------------------------------------------*/
/* USE_STDPERIPH_DRIVER 启用时由 stm32f10x.h 引入。下表控制可见的标准外设声明，
 * 外设是否配置与启用仍取决于板级代码及 Keil 工程的编译配置。 */
#include "stm32f10x_adc.h" /* 取得 ADC 初始化、校准和结果读取所需接口。 */
#include "stm32f10x_bkp.h" /* 引入 stm32f10x_bkp.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_can.h" /* 引入 stm32f10x_can.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_cec.h" /* 引入 stm32f10x_cec.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_crc.h" /* 引入 stm32f10x_crc.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_dac.h" /* 引入 stm32f10x_dac.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_dbgmcu.h" /* 引入 stm32f10x_dbgmcu.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_dma.h" /* 引入 stm32f10x_dma.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_exti.h" /* 引入 stm32f10x_exti.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_flash.h" /* 引入 stm32f10x_flash.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_fsmc.h" /* 引入 stm32f10x_fsmc.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_gpio.h" /* 引入 stm32f10x_gpio.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_i2c.h" /* 引入 stm32f10x_i2c.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_iwdg.h" /* 引入 stm32f10x_iwdg.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_pwr.h" /* 引入 stm32f10x_pwr.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_rcc.h" /* 引入 stm32f10x_rcc.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_rtc.h" /* 引入 stm32f10x_rtc.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_sdio.h" /* 引入 stm32f10x_sdio.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_spi.h" /* 引入 stm32f10x_spi.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_tim.h" /* 引入 stm32f10x_tim.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_usart.h" /* 引入 stm32f10x_usart.h 的声明和配置，供当前文件调用。 */
#include "stm32f10x_wwdg.h" /* 引入 stm32f10x_wwdg.h 的声明和配置，供当前文件调用。 */
#include "misc.h" /* High level functions for NVIC and SysTick (add-on to CMSIS functions) */ /* 引入 misc.h 的声明和配置，供当前文件调用。 */

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/
/* Uncomment the line below to expanse the "assert_param" macro in the 
   Standard Peripheral Library drivers code */
/* #define USE_FULL_ASSERT    1 */

/* Exported macro ------------------------------------------------------------*/
#ifdef  USE_FULL_ASSERT /* 仅在已定义 USE_FULL_ASSERT时编译下方代码。 */

/**
  * @brief  The assert_param macro is used for function's parameters check.
  * @param  expr: If expr is false, it calls assert_failed function which reports 
  *         the name of the source file and the source line number of the call 
  *         that failed. If expr is true, it returns no value.
  * @retval None
  */
  #define assert_param(expr) ((expr) ? (void)0 : assert_failed((uint8_t *)__FILE__, __LINE__)) /* 将 assert_param(expr) 展开为 「((expr) ? (void)0 : assert_failed((uint8_t *)__FILE__, __LINE__))」，供调用处复用同一操作。 */
/* Exported functions ------------------------------------------------------- */
  void assert_failed(uint8_t* file, uint32_t line); /* 声明接口，assert_failed。 */
#else /* 前面的编译条件不成立时，改用下方实现或配置。 */
  #define assert_param(expr) ((void)0) /* 将 assert_param(expr) 展开为 「((void)0)」，供调用处复用同一操作。 */
#endif /* USE_FULL_ASSERT */ /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

#endif /* __STM32F10x_CONF_H */ /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
