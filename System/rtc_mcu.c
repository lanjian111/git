#include "rtc_mcu.h" /* 取得 MCU RTC 配置和日期时间接口。 */
#include "stm32f10x.h" /* 使用 STM32F1 的寄存器、外设类型和基于工程配置启用的驱动接口。 */
#include <stdio.h> /* 取得 snprintf 等受缓冲区容量约束的格式化接口。 */

/*==========================================================================
 * RTC epoch: 2000-01-01 00:00:00
 * 32 位秒计数器可覆盖到 2136 年, 足够用
 *==========================================================================*/
#define RTC_EPOCH_YEAR          2000U /* 定义 实时时钟 EPOCH 年份 的约定值 2000U，使对应判断或封装统一引用。 */

/* BKP 备份域寄存器: 标记 RTC 已配置 (防止重复初始化) */
#define BKP_DR1_RTC_MAGIC       0x5A5A /* 定义 BKP DR 1 实时时钟 MAGIC 的约定值 0x5A5A，使对应判断或封装统一引用。 */

/*==========================================================================
 * 工具函数
 *==========================================================================*/

static uint8_t _is_leap(uint16_t year) /* 按公历规则判断该年二月是否有 29 天。 */
{
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0; /* 将 「((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0」 返回给调用者，供上层使用。 */
}

/* 从 RTC 秒计数器中计算年月日 */
static void _sec_to_date(uint32_t sec, uint16_t *year, uint8_t *month, uint8_t *day, uint8_t *week) /* 把自历元累计秒数转换成年月日和星期。 */
{
    static const uint8_t month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; /* 定义各月的天数查表数据，将各月的天数查表数据 更新为 「{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}」。 */
    uint32_t days = sec / 86400U; /* 定义天数，将天数 更新为 「sec / 86400U」。 */
    uint16_t y = RTC_EPOCH_YEAR; /* 定义y，将y 更新为 「RTC_EPOCH_YEAR」。 */
    uint8_t  m; /* 声明m，为后续计算或读取保存中间值。 */
    uint32_t d; /* 声明d，为后续计算或读取保存中间值。 */

    /* 从基准日期的星期累加天数。2000-01-01 是周六，按 0=周日编号为 6。 */
    *week = (uint8_t)((days + 6) % 7); /* 将星期 更新为 「(uint8_t)((days + 6) % 7)」。 */

    /* 逐年减 */
    while (1) /* 保持在循环中，依靠内部退出条件或外部复位结束。 */
    {
        uint16_t days_in_year = _is_leap(y) ? 366U : 365U; /* 定义天数 in 年份，将天数 in 年份 更新为 「_is_leap(y) ? 366U : 365U」。 */
        if (days < days_in_year) break; /* 当 「days < days_in_year」 成立时，结束本分支或当前循环，避免继续执行后面的分支或迭代。 */
        days -= days_in_year; /* 将天数 扣除 「days_in_year」。 */
        y++; /* 将y加一，推进处理进度。 */
    }
    *year = y; /* 将年份 更新为 「y」。 */

    /* 逐月减 */
    for (m = 0; m < 12; m++) /* 从 「m = 0」 开始，在 「m < 12」 成立时逐项处理，每轮执行 「m++」。 */
    {
        d = month_days[m]; /* 将d 更新为 「month_days[m]」。 */
        if (m == 1 && _is_leap(y)) d = 29; /* 当 「m == 1 && _is_leap(y)」 成立时，将d 更新为 「29」。 */
        if (days < d) break; /* 当 「days < d」 成立时，结束本分支或当前循环，避免继续执行后面的分支或迭代。 */
        days -= d; /* 将天数 扣除 「d」。 */
    }
    *month = m + 1; /* 将月份 更新为 「m + 1」。 */
    *day   = (uint8_t)(days + 1); /* 将日 更新为 「(uint8_t)(days + 1)」。 */
}

/* 将年月日转换为距 epoch 的天数 */
static uint32_t _date_to_days(uint16_t year, uint8_t month, uint8_t day) /* 把年月日转换成距 RTC 历元的天数。 */
{
    static const uint8_t month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; /* 定义各月的天数查表数据，将各月的天数查表数据 更新为 「{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}」。 */
    uint32_t days = 0; /* 定义天数，将天数 更新为 「0」。 */
    uint16_t y; /* 声明y，为后续计算或读取保存中间值。 */
    uint8_t  m; /* 声明m，为后续计算或读取保存中间值。 */

    for (y = RTC_EPOCH_YEAR; y < year; y++) /* 从 「y = RTC_EPOCH_YEAR」 开始，在 「y < year」 成立时逐项处理，每轮执行 「y++」。 */
        days += _is_leap(y) ? 366U : 365U; /* 将天数 累加 「_is_leap(y) ? 366U : 365U」。 */

    for (m = 1; m < month; m++) /* 从 「m = 1」 开始，在 「m < month」 成立时逐项处理，每轮执行 「m++」。 */
    {
        days += month_days[m - 1]; /* 将天数 累加 「month_days[m - 1]」。 */
        if (m == 2 && _is_leap(year)) days++; /* 当 「m == 2 && _is_leap(year)) days++」 成立时处理下方分支。 */
    }

    days += (uint32_t)(day - 1); /* 将天数 累加 「(uint32_t)(day - 1)」。 */
    return days; /* 将 「days」 返回给调用者，供上层使用。 */
}

/*==========================================================================
 * RTC_MCU_Init
 *==========================================================================*/
void RTC_MCU_Init(void) /* 选择 RTC 时钟源并配置备份域，首次配置时设置分频及初始化标志。 */
{
    /* 使能 PWR 和 BKP 时钟 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE); /* 开启 「RCC_APB1Periph_PWR | RCC_APB1Periph_BKP」 的外设时钟，控制相关外设时钟供给。 */
    PWR_BackupAccessCmd(ENABLE); /* 调用 PWR_BackupAccessCmd 处理 「ENABLE」。 */

    /* 检查是否已配置过 RTC */
    if (BKP_ReadBackupRegister(BKP_DR1) == BKP_DR1_RTC_MAGIC) /* 检查是否已配置过 RTC。 */
    {
        /* RTC 已配置, 确保时钟源开启后同步 */
#ifdef RTC_USE_LSE /* 仅在已定义 RTC_USE_LSE时编译下方代码。 */
        RCC_LSEConfig(RCC_LSE_ON); /* 调用 RCC_LSEConfig 处理 「RCC_LSE_ON」。 */
        while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET); /* 持续读状态直到 「RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET」 不再成立，通过空循环等待硬件就绪。 */
#else /* 前面的编译条件不成立时，改用下方实现或配置。 */
        RCC_LSICmd(ENABLE); /* 调用 RCC_LSICmd 处理 「ENABLE」。 */
        while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET); /* 持续读状态直到 「RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET」 不再成立，通过空循环等待硬件就绪。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
        RCC_RTCCLKCmd(ENABLE); /* 调用 RCC_RTCCLKCmd 处理 「ENABLE」。 */
        RTC_WaitForSynchro(); /* 执行 RTC_WaitForSynchro 的实时时钟 等候 For Synchro操作。 */
        return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */
    }

    /* 首次配置: 复位备份域 */
    BKP_DeInit(); /* 执行 BKP_DeInit 的BKP De 初始化操作。 */

#ifdef RTC_USE_LSE /* 仅在已定义 RTC_USE_LSE时编译下方代码。 */
    /* 外部 32.768kHz 晶振 */
    RCC_LSEConfig(RCC_LSE_ON); /* 调用 RCC_LSEConfig 处理 「RCC_LSE_ON」。 */
    while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET); /* 持续读状态直到 「RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET」 不再成立，通过空循环等待硬件就绪。 */
    RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE); /* 调用 RCC_RTCCLKConfig 处理 「RCC_RTCCLKSource_LSE」。 */
#else /* 前面的编译条件不成立时，改用下方实现或配置。 */
    /* 内部 LSI ~40kHz */
    RCC_LSICmd(ENABLE); /* 调用 RCC_LSICmd 处理 「ENABLE」。 */
    while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET); /* 持续读状态直到 「RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET」 不再成立，通过空循环等待硬件就绪。 */
    RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI); /* 调用 RCC_RTCCLKConfig 处理 「RCC_RTCCLKSource_LSI」。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

    RCC_RTCCLKCmd(ENABLE); /* 调用 RCC_RTCCLKCmd 处理 「ENABLE」。 */
    RTC_WaitForSynchro(); /* 执行 RTC_WaitForSynchro 的实时时钟 等候 For Synchro操作。 */

    RTC_SetPrescaler(32767);      /* LSE 32.768 kHz 时得到 1 Hz。LSI 分支也沿用此值，频率取决于实际 LSI。 */
    RTC_WaitForLastTask(); /* 执行 RTC_WaitForLastTask 的实时时钟 等候 For Last 任务操作。 */

    /* 标记已配置 */
    BKP_WriteBackupRegister(BKP_DR1, BKP_DR1_RTC_MAGIC); /* 调用 BKP_WriteBackupRegister 处理 「BKP_DR1, BKP_DR1_RTC_MAGIC」。 */
}

/*==========================================================================
 * RTC_MCU_GetTime
 *==========================================================================*/
void RTC_MCU_GetTime(RTC_Time_t *t) /* 读取 MCU RTC 累计秒数，拆解为日期和时间字段。 */
{
    uint32_t sec; /* 声明sec，为后续计算或读取保存中间值。 */

    sec = RTC_GetCounter(); /* 将sec 更新为 「RTC_GetCounter()」。 */
    t->second = (uint8_t)(sec % 60U); /* 将秒 更新为 「(uint8_t)(sec % 60U)」。 */
    t->minute = (uint8_t)((sec / 60U) % 60U); /* 将分 更新为 「(uint8_t)((sec / 60U) % 60U)」。 */
    t->hour   = (uint8_t)((sec / 3600U) % 24U); /* 将时 更新为 「(uint8_t)((sec / 3600U) % 24U)」。 */
    _sec_to_date(sec, &t->year, &t->month, &t->day, &t->week); /* 把自历元累计秒数转换成年月日和星期。 */
}

/*==========================================================================
 * RTC_MCU_SetTime
 *==========================================================================*/
void RTC_MCU_SetTime(const RTC_Time_t *t) /* 将给定日期时间折算成累计秒数，写入 MCU RTC。 */
{
    uint32_t days, sec; /* 声明 「days, sec」，用于本函数的中间结果和循环控制。 */

    days = _date_to_days(t->year, t->month, t->day); /* 将天数 更新为 「_date_to_days(t->year, t->month, t->day)」。 */
    sec  = days * 86400U; /* 将sec 更新为 「days * 86400U」。 */
    sec += (uint32_t)t->hour   * 3600U; /* 将sec 累加 「(uint32_t)t->hour * 3600U」。 */
    sec += (uint32_t)t->minute * 60U; /* 将sec 累加 「(uint32_t)t->minute * 60U」。 */
    sec += (uint32_t)t->second; /* 将sec 累加 「(uint32_t)t->second」。 */

    /* 等待前一次写入完成。标准库 RTC_SetCounter 内部负责进入和退出配置模式。 */
    RTC_WaitForLastTask(); /* 执行 RTC_WaitForLastTask 的实时时钟 等候 For Last 任务操作。 */
    RTC_SetCounter(sec); /* 调用 RTC_SetCounter 处理 「sec」。 */
    RTC_WaitForLastTask(); /* 执行 RTC_WaitForLastTask 的实时时钟 等候 For Last 任务操作。 */
}

/*==========================================================================
 * RTC_MCU_FormatHM — "HH:MM"
 *==========================================================================*/
void RTC_MCU_FormatHM(const RTC_Time_t *t, char *buf, uint8_t bufSize) /* 将时间格式化为两位小时与分钟，供上层显示。 */
{
    snprintf(buf, bufSize, "%02u:%02u", t->hour, t->minute); /* 在指定容量内格式化文本，约束目标缓冲区的写入范围。 */
}

/*==========================================================================
 * RTC_MCU_FormatFull — "YYYY-MM-DD HH:MM:SS"
 *==========================================================================*/
void RTC_MCU_FormatFull(const RTC_Time_t *t, char *buf, uint8_t bufSize) /* 将日期时间格式化成供显示的年月日、时分秒文本。 */
{
    snprintf(buf, bufSize, "%u-%02u-%02u %02u:%02u:%02u", /* 用 「snprintf(buf, bufSize, "%u-%02u-%02u %02u:%02u:%02u",」 补足上行的表达式、缓冲区内容或接口参数。 */
             t->year, t->month, t->day, t->hour, t->minute, t->second); /* 按年月日和时分秒的显示顺序传入格式化参数。 */
}

/*==========================================================================
 * RTC_BootRestore — 上电从 EEPROM 恢复或写默认值
 *==========================================================================*/
#include "rtc_storage.h" /* 取得日期校验和 EEPROM 时间记录接口。 */
#include "hmi_driver.h" /* 取得触摸屏命令封装和链路处理接口。 */
#include "delay.h" /* 使用 SysTick 毫秒计时和短时延时接口。 */
#include "elog.h" /* 取得日志级别、格式及输出入口的声明。 */

#define LOG_TAG     "rtc" /* 指定日志标签 "rtc"，便于在串口输出中定位本模块。 */

void RTC_BootRestore(void) /* 检查 RTC 备份域，必要时从 EEPROM 或默认值恢复时间。 */
{
    uint32_t ms = delay_millis(); /* 定义毫秒，把当前毫秒节拍保存到 ms，作为之后计算时间差的基准。 */
    while (!delay_expired(ms, 1500U)) { HMI_LinkTask(); } /* 在 「!delay_expired(ms, 1500U)」 成立期间重复执行 「HMI_LinkTask()」。 */

    /* 标记表示备份域曾配置。RTC_MCU_Init 在首次配置时也会写标记，
     * 因此若先调用初始化，本分支同样会跳过 EEPROM 恢复。 */
    if (BKP_ReadBackupRegister(BKP_DR1) == BKP_DR1_RTC_MAGIC) /* 当 「BKP_ReadBackupRegister(BKP_DR1) == BKP_DR1_RTC_MAGIC」 成立时处理下方分支。 */
    {
        RTC_Time_t t; /* 声明t，为后续计算或读取保存中间值。 */
        RTC_MCU_GetTime(&t); /* 读取 MCU RTC 累计秒数，拆解为日期和时间字段。 */
        log_i("RTC running: %u-%02u-%02u %02u:%02u", /* 开始构造当前分支的日志记录，格式串后的参数在下一行给出。 */
              t.year, t.month, t.day, t.hour, t.minute); /* 将本分支取得的年月日和时分传给日志，便于核对恢复结果。 */
        return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */
    }

    /* 首次上电/VBAT掉电: 从 EEPROM 恢复 */
    RTC_Time_t t; /* 声明t，为后续计算或读取保存中间值。 */
    if (RTC_LoadFromEEPROM(&t)) /* 当 「RTC_LoadFromEEPROM(&t)」 成立时处理下方分支。 */
    {
        RTC_MCU_SetTime(&t); /* 将给定日期时间折算成累计秒数，写入 MCU RTC。 */
        log_i("RTC from EEPROM: %u-%02u-%02u %02u:%02u", /* 开始构造当前分支的日志记录，格式串后的参数在下一行给出。 */
              t.year, t.month, t.day, t.hour, t.minute); /* 将本分支取得的年月日和时分传给日志，便于核对恢复结果。 */
    }
    else /* 前面的条件不满足时执行此分支。 */
    {
        t.year = 2026; t.month = 7;  t.day   = 12; /* 依次执行将年份 更新为 「2026」，将月份 更新为 「7」，将日 更新为 「12」。 */
        t.hour = 0;    t.minute = 0; t.second = 0; /* 依次执行将时 更新为 「0」，将分 更新为 「0」，将秒 更新为 「0」。 */
        t.week = RTC_CalcWeek(t.year, t.month, t.day); /* 将星期 更新为 「RTC_CalcWeek(t.year, t.month, t.day)」。 */
        RTC_MCU_SetTime(&t); /* 将给定日期时间折算成累计秒数，写入 MCU RTC。 */
        RTC_SaveToEEPROM(&t); /* 将有效时间打包后交给 EEPROM 驱动保存。 */
        log_i("RTC default: %u-%02u-%02u %02u:%02u", /* 开始构造当前分支的日志记录，格式串后的参数在下一行给出。 */
              t.year, t.month, t.day, t.hour, t.minute); /* 将本分支取得的年月日和时分传给日志，便于核对恢复结果。 */
    }
}
