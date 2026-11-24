#include "rtc_screen.h"
#include "delay.h"
#include "hmi_driver.h"
#include "elog.h"

#define LOG_TAG "rtc"
#define SYNC_INTERVAL_MS 300000U
#define REPLY_TIMEOUT_MS 500U
#define RETRY_INTERVAL_MS 5000U
#define MS_PER_SECOND 1000U
#define SECONDS_PER_DAY 86400U

/* Accessed only from the main loop and its HMI dispatch. */
static RTC_Time_t screen_time;
static uint8_t time_valid;
static uint32_t clock_tick;
static uint8_t sync_requested = 1U;
static uint8_t waiting_for_reply;
static uint32_t request_tick;
static uint32_t schedule_tick;
static uint32_t schedule_interval;

static uint8_t decode_bcd(uint8_t value)
{
    uint8_t tens = value >> 4;
    uint8_t ones = value & 0x0FU;

    if (tens > 9U || ones > 9U)
        return 0xFFU;

    return (uint8_t)(tens * 10U + ones);
}

static void advance_clock(uint32_t now)
{
    uint32_t elapsed_seconds = (now - clock_tick) / MS_PER_SECOND;
    uint32_t seconds_today;

    if (elapsed_seconds == 0U)
        return;

    clock_tick += elapsed_seconds * MS_PER_SECOND;
    seconds_today = screen_time.hour * 3600U + screen_time.minute * 60U
                  + screen_time.second + elapsed_seconds;

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

    if (time_valid)
        advance_clock(now);

    if (waiting_for_reply)
    {
        if ((now - request_tick) < REPLY_TIMEOUT_MS)
            return;

        waiting_for_reply = 0U;
        schedule_tick = now;
        schedule_interval = RETRY_INTERVAL_MS;
    }

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
    sync_requested = 1U;
}

void RTC_Screen_OnRead(const uint8_t data[7])
{
    RTC_Time_t received;
    uint32_t now = delay_millis();

    if (!waiting_for_reply || (now - request_tick) >= REPLY_TIMEOUT_MS)
        return;

    received.year = (uint16_t)(2000U + decode_bcd(data[0]));
    received.month = decode_bcd(data[1]);
    received.week = decode_bcd(data[2]);
    received.day = decode_bcd(data[3]);
    received.hour = decode_bcd(data[4]);
    received.minute = decode_bcd(data[5]);
    received.second = decode_bcd(data[6]);

    if (received.week > 6U || !RTC_IsValid(&received))
        return;

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
    if (!time_valid || time == 0)
        return 0U;

    advance_clock(delay_millis());
    *time = screen_time;
    return 1U;
}
