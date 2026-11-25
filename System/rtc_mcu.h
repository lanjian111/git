#ifndef __RTC_MCU_H
#define __RTC_MCU_H

#include <stdint.h>
#include "rtc_storage.h"    /* 使用统一的 RTC_Time_t 类型 */

/*==========================================================================
 * 旧版 STM32 内部 RTC 驱动，当前 Keil 目标未编入，由 rtc_screen 提供运行时钟。
 * 此文件定义 RTC_USE_LSE，选用 32.768 kHz 外部晶振。LSE 与有效 VBAT 供电可支持掉电走时。
 *==========================================================================*/

#define RTC_USE_LSE   /* 使用外部 32.768kHz 晶振 */

/*==========================================================================
 * 接口
 *==========================================================================*/

/* 初始化备份域和时钟源。晶振就绪及寄存器同步均为阻塞等待。 */
void RTC_MCU_Init(void);
void RTC_MCU_GetTime(RTC_Time_t *t);
/* 从 2000-01-01 起换算秒数，调用方保证日期有效且可由 32 位秒数表示。 */
void RTC_MCU_SetTime(const RTC_Time_t *t);
/* 旧恢复入口。按备份域标记决定是否读 EEPROM 或写默认时间。 */
void RTC_BootRestore(void);
/* bufSize 包含结束符；完整 HH:MM 需要 6 字节，完整 YYYY-MM-DD HH:MM:SS 需要 20 字节。 */
void RTC_MCU_FormatHM(const RTC_Time_t *t, char *buf, uint8_t bufSize);
void RTC_MCU_FormatFull(const RTC_Time_t *t, char *buf, uint8_t bufSize);

#endif
