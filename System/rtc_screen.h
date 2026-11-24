#ifndef RTC_SCREEN_H
#define RTC_SCREEN_H

#include "rtc_storage.h"

void RTC_Screen_Task(void);
void RTC_Screen_RequestSync(void);
void RTC_Screen_OnRead(const uint8_t data[7]);

/* The date and time are usable after the first valid screen reply. */
uint8_t RTC_Screen_GetTime(RTC_Time_t *time);

#endif
