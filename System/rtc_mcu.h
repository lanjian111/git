#ifndef __RTC_MCU_H /* 仅在尚未定义 __RTC_MCU_H时编译下方代码。 */
#define __RTC_MCU_H /* 定义 __RTC_MCU_H，供头文件防重包含或后续条件编译检查。 */

#include <stdint.h> /* 使用位宽明确的整数类型，匹配寄存器宽度及协议字段。 */
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
void RTC_MCU_Init(void); /* 声明接口，选择 RTC 时钟源并配置备份域，首次配置时设置分频及初始化标志。 */
void RTC_MCU_GetTime(RTC_Time_t *t); /* 声明接口，读取 MCU RTC 累计秒数，拆解为日期和时间字段。 */
/* 从 2000-01-01 起换算秒数，调用方保证日期有效且可由 32 位秒数表示。 */
void RTC_MCU_SetTime(const RTC_Time_t *t); /* 声明接口，将给定日期时间折算成累计秒数，写入 MCU RTC。 */
/* 旧恢复入口。按备份域标记决定是否读 EEPROM 或写默认时间。 */
void RTC_BootRestore(void); /* 声明接口，检查 RTC 备份域，必要时从 EEPROM 或默认值恢复时间。 */
/* bufSize 包含结束符；完整 HH:MM 需要 6 字节，完整 YYYY-MM-DD HH:MM:SS 需要 20 字节。 */
void RTC_MCU_FormatHM(const RTC_Time_t *t, char *buf, uint8_t bufSize); /* 声明接口，将时间格式化为两位小时与分钟，供上层显示。 */
void RTC_MCU_FormatFull(const RTC_Time_t *t, char *buf, uint8_t bufSize); /* 声明接口，将日期时间格式化成供显示的年月日、时分秒文本。 */

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
