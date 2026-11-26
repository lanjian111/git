#ifndef RTC_SCREEN_H /* 仅在尚未定义 RTC_SCREEN_H时编译下方代码。 */
#define RTC_SCREEN_H /* 定义 RTC_SCREEN_H，供头文件防重包含或后续条件编译检查。 */

#include "rtc_storage.h" /* 取得日期校验和 EEPROM 时间记录接口。 */

/* 主循环调用一次即可推进缓存、定时校准或超时重试，每次调用均无需等待屏幕。 */
void RTC_Screen_Task(void); /* 声明接口，推进本地时间并检查校时调度，等待屏幕回包期间保持周期任务可运行。 */

/* 屏幕保存 RTC 后调用，通知主循环尽早发一次读时指令。 */
void RTC_Screen_RequestSync(void); /* 声明接口，置主动读时请求，由屏幕 RTC 任务安排发送。 */

/* HMI 在主循环分发时调用。data 指向 7 字节 BCD，依次为年、月、星期、日、时、分、秒。 */
void RTC_Screen_OnRead(const uint8_t data[7]); /* 声明接口，处理屏幕返回的七个 BCD 字节，验证后更新时间缓存。 */

/* 首次收到合法回包后才可能返回 1，并把当前时间写入 time。返回 0 时不改写 time。 */
uint8_t RTC_Screen_GetTime(RTC_Time_t *time); /* 声明接口，向调用方提供已同步的时间副本，未同步或指针为空时报告失败。 */

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
