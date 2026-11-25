#ifndef __RTC_STORAGE_H
#define __RTC_STORAGE_H

#include <stdint.h>

/*==========================================================================
 * RTC 时间存储模块
 *
 * 公共时间类型、日期校验、月长计算和旧 EEPROM 存取接口。
 * 当前主循环通过 rtc_screen 从屏幕同步时间，只维护 RAM 中的本地缓存。
 * EEPROM 时间存取仅供旧 MCU RTC 恢复路径使用。
 *
 * EEPROM 从 RTC_EEPROM_ADDR 起存 8 字节二进制数据。
 * [0~1] 年份高、低字节，[2] 月，[3] 日，[4] 时，[5] 分，[6] 秒，[7] 星期。
 * 屏幕 0x83 写时接口接受同样的字段顺序。0xF7 读时回包采用另一套 BCD 布局，由 rtc_screen 解析。
 *==========================================================================*/

#define RTC_EEPROM_ADDR         0x0000U     /* EEPROM 中 RTC 数据起始地址     */
#define RTC_DATA_SIZE           8U          /* RTC 数据长度 (字节)            */

/* 时间数据结构 (二进制) */
typedef struct {
    uint16_t year;      /* 年, 如 2026                            */
    uint8_t  month;     /* 月, 1~12                              */
    uint8_t  day;       /* 日, 1~31                              */
    uint8_t  hour;      /* 时, 0~23                              */
    uint8_t  minute;    /* 分, 0~59                              */
    uint8_t  second;    /* 秒, 0~59                              */
    uint8_t  week;      /* 星期, 0~6 (0=周日)                    */
} RTC_Time_t;

/*==========================================================================
 * 格式转换
 *==========================================================================*/

/* 历史声明。当前源码无对应外部实现，屏幕读时解码见 rtc_screen.c。 */
uint8_t  RTC_Bcd2Bin(uint8_t bcd);

/* 历史声明。当前源码无对应外部实现。 */
uint8_t  RTC_Bin2Bcd(uint8_t bin);

/* 将二进制 RTC_Time_t 打包为 8 字节大端数组 (用于 EEPROM) */
void     RTC_Pack(const RTC_Time_t *t, uint8_t out[8]);

/* 从 8 字节大端数组解析为 RTC_Time_t */
void     RTC_Unpack(const uint8_t raw[8], RTC_Time_t *t);

/*==========================================================================
 * 校验与计算
 *==========================================================================*/

/* 公历蔡勒公式，返回 0=周六、1=周日、2=周一，以此类推。
 * RTC_Time_t.week 使用 0=周日。直接把此返回值作为 week 写入会偏移一天。 */
uint8_t  RTC_CalcWeek(uint16_t year, uint8_t month, uint8_t day);

/* 校验 2000~2099 年的公历日期和时分秒，返回 1 代表有效。星期由调用方另行校验。 */
uint8_t  RTC_IsValid(const RTC_Time_t *t);
/* 返回指定年月的天数，闰年二月为 29 天。月份越界时返回 0。 */
uint8_t  RTC_DaysInMonth(uint16_t year, uint8_t month);

/*==========================================================================
 * EEPROM 存取
 *==========================================================================*/

/* 同步读取并解包，返回 RTC_IsValid 的结果。当前实现未单独处理 EEPROM 读 NACK。 */
uint8_t  RTC_LoadFromEEPROM(RTC_Time_t *t);
/* 返回 0 表示 EEPROM 驱动接收请求，返回 1 表示拒绝。最终状态由 EEPROM_GetState 查询。 */
uint8_t  RTC_SaveToEEPROM(const RTC_Time_t *t);

#endif /* __RTC_STORAGE_H */
