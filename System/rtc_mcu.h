#ifndef __RTC_MCU_H
#define __RTC_MCU_H

#include <stdint.h>
#include "rtc_storage.h"    /* 使用统一的 RTC_Time_t 类型 */

/*==========================================================================
 * STM32 内部 RTC 驱动 (VBAT 电池供电, 断电继续走时)
 *
 * 时钟源: LSI (~40kHz), 精度偏低但不需要外部晶振
 * 如果接了外部 32.768kHz 晶振 (PC14/PC15), 定义 RTC_USE_LSE
 *==========================================================================*/

#define RTC_USE_LSE   /* 使用外部 32.768kHz 晶振 */

/*==========================================================================
 * 接口
 *==========================================================================*/

void RTC_MCU_Init(void);
void RTC_MCU_GetTime(RTC_Time_t *t);
void RTC_MCU_SetTime(const RTC_Time_t *t);
void RTC_BootRestore(void);
void RTC_MCU_FormatHM(const RTC_Time_t *t, char *buf, uint8_t bufSize);
void RTC_MCU_FormatFull(const RTC_Time_t *t, char *buf, uint8_t bufSize);

#endif
