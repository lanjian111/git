#ifndef __DELAY_H
#define __DELAY_H 			   
#include "sys.h"


/* 初始化 SysTick 和 DWT 周期计数器，应在使用以下计时接口前调用。 */
void delay_init(void);
/* 按 CPU 周期忙等 nus 微秒，调用期间占用前台执行时间。 */
void delay_us(u32 nus);

/* 1 ms 无符号节拍约每 49.7 天回绕。起止相隔少于一个完整回绕时，可用无符号差值计时。 */
uint32_t delay_tick_inc(void);                                      // 每1ms调用一次，增加节拍计数
uint32_t delay_millis(void);                                        // 获取当前毫秒节拍计数  
bool delay_expired(uint32_t start_ms, uint32_t timeout_ms);         // 判断是否超时

/* 按 SysTick 节拍忙等 nms 毫秒。用于状态机的时间判断走 delay_expired()。 */
void delay_ms(u16 nms);

	
#endif





























