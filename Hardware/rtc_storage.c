#include "rtc_storage.h"
#include "eeprom_cat24c256.h"

/*==========================================================================
 * 打包 / 解包 (大端序二进制)
 *==========================================================================*/
void RTC_Pack(const RTC_Time_t *t, uint8_t out[8])
{
    out[0] = (uint8_t)(t->year >> 8);       /* 年 高字节 */
    out[1] = (uint8_t)(t->year & 0xFF);     /* 年 低字节 */
    out[2] = t->month;
    out[3] = t->day;
    out[4] = t->hour;
    out[5] = t->minute;
    out[6] = t->second;
    out[7] = t->week;
}

void RTC_Unpack(const uint8_t raw[8], RTC_Time_t *t)
{
    t->year   = ((uint16_t)raw[0] << 8) | raw[1];
    t->month  = raw[2];
    t->day    = raw[3];
    t->hour   = raw[4];
    t->minute = raw[5];
    t->second = raw[6];
    t->week   = raw[7];
}

/*==========================================================================
 * 公历闰年。整百年须能被 400 整除，其他年份能被 4 整除即可。
 *==========================================================================*/
static uint8_t _is_leap(uint16_t year)
{
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
}

/*==========================================================================
 * 公共日历计算，供日期合法性校验和屏幕时钟跨月时共用
 *==========================================================================*/
uint8_t RTC_DaysInMonth(uint16_t year, uint8_t month)
{
    /* 月份先做 1 至 12 检查，再转换成数组下标。闰年只影响二月的天数。 */
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 0 || month > 12) return 0;
    if (month == 2 && _is_leap(year)) return 29;
    return days[month - 1];
}

/*==========================================================================
 * 公历蔡勒公式的原始编号，0=周六、1=周日。
 *==========================================================================*/
uint8_t RTC_CalcWeek(uint16_t year, uint8_t month, uint8_t day)
{
    uint16_t y;
    uint8_t  m;
    int      h;

    if (month < 3)
    {
        y = year - 1;
        m = month + 12;
    }
    else
    {
        y = year;
        m = month;
    }

    /* 一月、二月按上一年的 13、14 月计算。世纪项可使余数为负，下方归一化到 0~6。 */
    h = ((int)day + (13 * (m + 1)) / 5 + (y % 100) + ((y % 100) / 4)
         + ((y / 100) / 4) - 2 * (y / 100)) % 7;

    if (h < 0) h += 7;

    return (uint8_t)h;  /* 蔡勒编号，0=周六 */
}

/*==========================================================================
 * 检查年份、年月日和时分秒。函数要求 t 有效，星期由调用方检查。
 *==========================================================================*/
uint8_t RTC_IsValid(const RTC_Time_t *t)
{
    uint8_t max_day;

    if (t->year < 2000 || t->year > 2099)  return 0;
    if (t->month < 1   || t->month > 12)   return 0;
    if (t->hour > 23)                       return 0;
    if (t->minute > 59)                     return 0;
    if (t->second > 59)                     return 0;

    max_day = RTC_DaysInMonth(t->year, t->month);
    if (t->day < 1 || t->day > max_day)     return 0;

    return 1;
}

/*==========================================================================
 * EEPROM 读取 (同步)
 *==========================================================================*/
uint8_t RTC_LoadFromEEPROM(RTC_Time_t *t)
{
    uint8_t raw[8];

    /* 当前代码无论总线结果如何都会解包。读取遇到 NACK 时，底层可能未写入 raw。 */
    EEPROM_ReadBuffer(RTC_EEPROM_ADDR, raw, RTC_DATA_SIZE);
    RTC_Unpack(raw, t);

    return RTC_IsValid(t);
}

/*==========================================================================
 * EEPROM 写入 (异步, 由 EEPROM_Task 在 100ms 任务中推进)
 *==========================================================================*/
uint8_t RTC_SaveToEEPROM(const RTC_Time_t *t)
{
    uint8_t raw[8];
    /* 当前固定地址和长度可在第一页内一次送完，EEPROM_WriteBuffer 同步发送这一页，
     * 后续任务只等待芯片写入周期。若布局改为跨页，须保证缓冲区在后续页发送时仍有效。 */
    RTC_Pack(t, raw);
    return (EEPROM_WriteBuffer(RTC_EEPROM_ADDR, raw, RTC_DATA_SIZE) == EEPROM_OK) ? 0 : 1;
}
