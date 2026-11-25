#include "rtc_screen.h"
#include "delay.h"
#include "hmi_driver.h"
#include "elog.h"

#define LOG_TAG "rtc"
/* 屏幕 RTC 提供日期时间。平时用 STM32 的毫秒节拍推进缓存，定期从屏幕校准。
 * 成功后 5 分钟同步一次。单次等回包 500 ms，超时后 5 秒再试，避免一直占用串口。
 */
#define SYNC_INTERVAL_MS 300000U
#define REPLY_TIMEOUT_MS 500U
#define RETRY_INTERVAL_MS 5000U
#define MS_PER_SECOND 1000U
#define SECONDS_PER_DAY 86400U

/* HMI 串口收到的数据先排队，以下状态只在主循环和它调用的分发函数中访问。 */
static RTC_Time_t screen_time;        /* 最近一次有效校时后继续推进的本地缓存 */
static uint8_t time_valid;             /* 收到第一条合法屏幕回包后才置位 */
static uint32_t clock_tick;            /* 缓存已累计到的最近一个整秒对应的毫秒节拍 */
static uint8_t sync_requested = 1U;    /* 开机主动读一次，屏幕保存时间时也会置位 */
static uint8_t waiting_for_reply;      /* 已送出读命令，目前有一条请求在等待回应 */
static uint32_t request_tick;          /* 当前请求发出的时刻，用于判断回包是否超时 */
static uint32_t schedule_tick;         /* 常规校时或重试的计时起点 */
static uint32_t schedule_interval;     /* 从计时起点到下一次请求应等待的毫秒数 */

static uint8_t decode_bcd(uint8_t value)
{
    /* 屏幕用 BCD 表示每个字段，例如 0x25 表示十进制 25。
     * 半字节大于 9 就是非法编码，返回 0xFF 供后续日期和范围校验拒绝。
     */
    uint8_t tens = value >> 4;
    uint8_t ones = value & 0x0FU;

    if (tens > 9U || ones > 9U)
        return 0xFFU;

    return (uint8_t)(tens * 10U + ones);
}

static void advance_clock(uint32_t now)
{
    /* 用无符号节拍做差，SysTick 计数从最大值回到 0 时仍能计算经过时间。 */
    uint32_t elapsed_seconds = (now - clock_tick) / MS_PER_SECOND;
    uint32_t seconds_today;

    if (elapsed_seconds == 0U)
        return;

    /* 只扣除实际累计的整秒。余下的毫秒留到下次，否则每次调用都会丢掉余数。 */
    clock_tick += elapsed_seconds * MS_PER_SECOND;
    seconds_today = screen_time.hour * 3600U + screen_time.minute * 60U
                  + screen_time.second + elapsed_seconds;

    /* 将当天秒数归约到 0 至 86399。每跨一天都推进日期和星期。
     * 月末使用公共日历函数处理月长度和闰年，12 月后换到次年 1 月。
     */
    while (seconds_today >= SECONDS_PER_DAY)
    {
        seconds_today -= SECONDS_PER_DAY;
        screen_time.week = (uint8_t)((screen_time.week + 1U) % 7U);
        if (++screen_time.day > RTC_DaysInMonth(screen_time.year, screen_time.month))
        {
            screen_time.day = 1U;
            if (++screen_time.month > 12U)
            {
                screen_time.month = 1U;
                screen_time.year++;
            }
        }
    }

    screen_time.hour = (uint8_t)(seconds_today / 3600U);
    screen_time.minute = (uint8_t)((seconds_today / 60U) % 60U);
    screen_time.second = (uint8_t)(seconds_today % 60U);
}

void RTC_Screen_Task(void)
{
    uint32_t now = delay_millis();

    /* 校时是在后台推进的。即使屏幕没有回应，已校过的本地时间仍继续走。 */
    if (time_valid)
        advance_clock(now);

    /* 同一时刻保留一条请求。等待窗口内立即返回，主循环可继续处理机器控制。
     * 超时则清等待状态、安排延后重试。若此时另有手动同步请求，下面可优先尝试。
     */
    if (waiting_for_reply)
    {
        if ((now - request_tick) < REPLY_TIMEOUT_MS)
            return;

        waiting_for_reply = 0U;
        schedule_tick = now;
        schedule_interval = RETRY_INTERVAL_MS;
    }

    /* 保存时间的通知或到达周期都可以触发读取。只有 ReadRTC 确认发起发送，
     * 才开始计算回包超时。若屏幕串口正在忙，保留请求条件供下一轮重试。
     */
    if (sync_requested || (now - schedule_tick) >= schedule_interval)
    {
        if (ReadRTC())
        {
            sync_requested = 0U;
            waiting_for_reply = 1U;
            request_tick = now;
        }
    }
}

void RTC_Screen_RequestSync(void)
{
    /* 回调里只记下重读请求，实际发送统一走 RTC_Screen_Task()。 */
    sync_requested = 1U;
}

void RTC_Screen_OnRead(const uint8_t data[7])
{
    RTC_Time_t received;
    uint32_t now = delay_millis();

    /* 只在当前请求的等待窗口内使用读时回包，忽略请求之外或已超时的回包。 */
    if (!waiting_for_reply || (now - request_tick) >= REPLY_TIMEOUT_MS)
        return;

    /* 7 个字段依照屏幕帧顺序解析。年份只有两位，按 2000 至 2099 解释。 */
    received.year = (uint16_t)(2000U + decode_bcd(data[0]));
    received.month = decode_bcd(data[1]);
    received.week = decode_bcd(data[2]);
    received.day = decode_bcd(data[3]);
    received.hour = decode_bcd(data[4]);
    received.minute = decode_bcd(data[5]);
    received.second = decode_bcd(data[6]);

    /* 星期只检查编号范围，不与年月日重新匹配。公历日期和时分秒由 RTC_IsValid 校验。
     * 非法回包保留旧缓存和本次等待状态，让同一窗口内后续合法回包仍可生效。 */
    if (received.week > 6U || !RTC_IsValid(&received))
        return;

    /* 用回包处理时刻重置本地秒节拍基准，并从此时起安排下一次常规同步。 */
    screen_time = received;
    time_valid = 1U;
    clock_tick = now;
    waiting_for_reply = 0U;
    schedule_tick = now;
    schedule_interval = SYNC_INTERVAL_MS;
    log_i("Screen RTC: %u-%02u-%02u %02u:%02u:%02u",
          (unsigned)received.year, (unsigned)received.month, (unsigned)received.day,
          (unsigned)received.hour, (unsigned)received.minute, (unsigned)received.second);
}

uint8_t RTC_Screen_GetTime(RTC_Time_t *time)
{
    /* 第一次校时前，调用方收到 0，输出缓冲区也不被改写。成功时先推进到当前秒。 */
    if (!time_valid || time == 0)
        return 0U;

    advance_clock(delay_millis());
    *time = screen_time;
    return 1U;
}
