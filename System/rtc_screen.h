#ifndef RTC_SCREEN_H
#define RTC_SCREEN_H

#include "rtc_storage.h"

/* 主循环调用一次即可推进缓存、定时校准或超时重试，每次调用均无需等待屏幕。 */
void RTC_Screen_Task(void);

/* 屏幕保存 RTC 后调用，通知主循环尽早发一次读时指令。 */
void RTC_Screen_RequestSync(void);

/* HMI 在主循环分发时调用。data 指向 7 字节 BCD，依次为年、月、星期、日、时、分、秒。 */
void RTC_Screen_OnRead(const uint8_t data[7]);

/* 首次收到合法回包后才可能返回 1，并把当前时间写入 time。返回 0 时不改写 time。 */
uint8_t RTC_Screen_GetTime(RTC_Time_t *time);

#endif
