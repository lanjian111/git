#ifndef __RTC_STORAGE_H
#define __RTC_STORAGE_H

#include <stdint.h>

/*==========================================================================
 * RTC 时间存储模块
 *
 * 功能:
 *   1. 上电时从屏幕 RTC 读取时间并备份到 EEPROM
 *   2. 用户设置时间后写入屏幕 RTC + EEPROM 双保险
 *   3. 屏幕 RTC 电池没电时从 EEPROM 兜底恢复
 *
 * EEPROM 布局: 地址 RTC_EEPROM_ADDR 起存 8 字节 (二进制, 大端序)
 *   [0~1] 年 (uint16), [2]月, [3]日, [4]时, [5]分, [6]秒, [7]星期
 *
 * 编码说明:
 *   - EEPROM 存储和 WriteRTC(0x83) 均为二进制格式
 *   - 屏幕 ReadRTC(0x82) 返回 NOTIFY_READ_RTC(0xF7) 为 BCD 格式
 *   - BCD ? 二进制转换由本模块内部处理
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

/* BCD → 二进制 (0x59 → 59) */
uint8_t  RTC_Bcd2Bin(uint8_t bcd);

/* 二进制 → BCD (59 → 0x59) */
uint8_t  RTC_Bin2Bcd(uint8_t bin);

/* 将二进制 RTC_Time_t 打包为 8 字节大端数组 (用于 EEPROM) */
void     RTC_Pack(const RTC_Time_t *t, uint8_t out[8]);

/* 从 8 字节大端数组解析为 RTC_Time_t */
void     RTC_Unpack(const uint8_t raw[8], RTC_Time_t *t);

/*==========================================================================
 * 校验与计算
 *==========================================================================*/

/* 根据年月日计算星期 (蔡勒公式, 返回 0~6, 0=周日) */
uint8_t  RTC_CalcWeek(uint16_t year, uint8_t month, uint8_t day);

/* 校验时间是否合法 (范围 + 日期上限) */
uint8_t  RTC_IsValid(const RTC_Time_t *t);

/*==========================================================================
 * EEPROM 存取
 *==========================================================================*/

uint8_t  RTC_LoadFromEEPROM(RTC_Time_t *t);
uint8_t  RTC_SaveToEEPROM(const RTC_Time_t *t);

#endif /* __RTC_STORAGE_H */
