#include "rtc_mcu.h"
#include "stm32f10x.h"
#include <stdio.h>

/*==========================================================================
 * RTC epoch: 2000-01-01 00:00:00
 * 32 位秒计数器可覆盖到 2136 年, 足够用
 *==========================================================================*/
#define RTC_EPOCH_YEAR          2000U

/* BKP 备份域寄存器: 标记 RTC 已配置 (防止重复初始化) */
#define BKP_DR1_RTC_MAGIC       0x5A5A

/*==========================================================================
 * 工具函数
 *==========================================================================*/

static uint8_t _is_leap(uint16_t year)
{
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
}

/* 从 RTC 秒计数器中计算年月日 */
static void _sec_to_date(uint32_t sec, uint16_t *year, uint8_t *month, uint8_t *day, uint8_t *week)
{
    static const uint8_t month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint32_t days = sec / 86400U;
    uint16_t y = RTC_EPOCH_YEAR;
    uint8_t  m;
    uint32_t d;

    /* 蔡勒公式计算星期: 2000-01-01 是周六 (6) */
    *week = (uint8_t)((days + 6) % 7);

    /* 逐年减 */
    while (1)
    {
        uint16_t days_in_year = _is_leap(y) ? 366U : 365U;
        if (days < days_in_year) break;
        days -= days_in_year;
        y++;
    }
    *year = y;

    /* 逐月减 */
    for (m = 0; m < 12; m++)
    {
        d = month_days[m];
        if (m == 1 && _is_leap(y)) d = 29;
        if (days < d) break;
        days -= d;
    }
    *month = m + 1;
    *day   = (uint8_t)(days + 1);
}

/* 将年月日转换为距 epoch 的天数 */
static uint32_t _date_to_days(uint16_t year, uint8_t month, uint8_t day)
{
    static const uint8_t month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint32_t days = 0;
    uint16_t y;
    uint8_t  m;

    for (y = RTC_EPOCH_YEAR; y < year; y++)
        days += _is_leap(y) ? 366U : 365U;

    for (m = 1; m < month; m++)
    {
        days += month_days[m - 1];
        if (m == 2 && _is_leap(year)) days++;
    }

    days += (uint32_t)(day - 1);
    return days;
}

/*==========================================================================
 * RTC_MCU_Init
 *==========================================================================*/
void RTC_MCU_Init(void)
{
    /* 使能 PWR 和 BKP 时钟 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    /* 检查是否已配置过 RTC */
    if (BKP_ReadBackupRegister(BKP_DR1) == BKP_DR1_RTC_MAGIC)
    {
        /* RTC 已配置, 确保时钟源开启后同步 */
#ifdef RTC_USE_LSE
        RCC_LSEConfig(RCC_LSE_ON);
        while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET);
#else
        RCC_LSICmd(ENABLE);
        while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET);
#endif
        RCC_RTCCLKCmd(ENABLE);
        RTC_WaitForSynchro();
        return;
    }

    /* 首次配置: 复位备份域 */
    BKP_DeInit();

#ifdef RTC_USE_LSE
    /* 外部 32.768kHz 晶振 */
    RCC_LSEConfig(RCC_LSE_ON);
    while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET);
    RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
#else
    /* 内部 LSI ~40kHz */
    RCC_LSICmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET);
    RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);
#endif

    RCC_RTCCLKCmd(ENABLE);
    RTC_WaitForSynchro();

    RTC_SetPrescaler(32767);      /* 32768Hz / 32768 = 1Hz */
    RTC_WaitForLastTask();

    /* 标记已配置 */
    BKP_WriteBackupRegister(BKP_DR1, BKP_DR1_RTC_MAGIC);
}

/*==========================================================================
 * RTC_MCU_GetTime
 *==========================================================================*/
void RTC_MCU_GetTime(RTC_Time_t *t)
{
    uint32_t sec;

    sec = RTC_GetCounter();
    t->second = (uint8_t)(sec % 60U);
    t->minute = (uint8_t)((sec / 60U) % 60U);
    t->hour   = (uint8_t)((sec / 3600U) % 24U);
    _sec_to_date(sec, &t->year, &t->month, &t->day, &t->week);
}

/*==========================================================================
 * RTC_MCU_SetTime
 *==========================================================================*/
void RTC_MCU_SetTime(const RTC_Time_t *t)
{
    uint32_t days, sec;

    days = _date_to_days(t->year, t->month, t->day);
    sec  = days * 86400U;
    sec += (uint32_t)t->hour   * 3600U;
    sec += (uint32_t)t->minute * 60U;
    sec += (uint32_t)t->second;

    /* 写 RTC 计数器前需先进入配置模式 */
    RTC_WaitForLastTask();
    RTC_SetCounter(sec);
    RTC_WaitForLastTask();
}

/*==========================================================================
 * RTC_MCU_FormatHM — "HH:MM"
 *==========================================================================*/
void RTC_MCU_FormatHM(const RTC_Time_t *t, char *buf, uint8_t bufSize)
{
    snprintf(buf, bufSize, "%02u:%02u", t->hour, t->minute);
}

/*==========================================================================
 * RTC_MCU_FormatFull — "YYYY-MM-DD HH:MM:SS"
 *==========================================================================*/
void RTC_MCU_FormatFull(const RTC_Time_t *t, char *buf, uint8_t bufSize)
{
    snprintf(buf, bufSize, "%u-%02u-%02u %02u:%02u:%02u",
             t->year, t->month, t->day, t->hour, t->minute, t->second);
}

/*==========================================================================
 * RTC_BootRestore — 上电从 EEPROM 恢复或写默认值
 *==========================================================================*/
#include "rtc_storage.h"
#include "hmi_driver.h"
#include "delay.h"
#include "elog.h"

#define LOG_TAG     "rtc"

void RTC_BootRestore(void)
{
    uint32_t ms = delay_millis();
    while (!delay_expired(ms, 1500U)) { HMI_LinkTask(); }

    /* RTC 已配置 (VBAT 供电, 一直在走): 不覆盖 */
    if (BKP_ReadBackupRegister(BKP_DR1) == BKP_DR1_RTC_MAGIC)
    {
        RTC_Time_t t;
        RTC_MCU_GetTime(&t);
        log_i("RTC running: %u-%02u-%02u %02u:%02u",
              t.year, t.month, t.day, t.hour, t.minute);
        return;
    }

    /* 首次上电/VBAT掉电: 从 EEPROM 恢复 */
    RTC_Time_t t;
    if (RTC_LoadFromEEPROM(&t))
    {
        RTC_MCU_SetTime(&t);
        log_i("RTC from EEPROM: %u-%02u-%02u %02u:%02u",
              t.year, t.month, t.day, t.hour, t.minute);
    }
    else
    {
        t.year = 2026; t.month = 7;  t.day   = 12;
        t.hour = 0;    t.minute = 0; t.second = 0;
        t.week = RTC_CalcWeek(t.year, t.month, t.day);
        RTC_MCU_SetTime(&t);
        RTC_SaveToEEPROM(&t);
        log_i("RTC default: %u-%02u-%02u %02u:%02u",
              t.year, t.month, t.day, t.hour, t.minute);
    }
}
