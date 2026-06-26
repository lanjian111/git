#ifndef __DELAY_H
#define __DELAY_H 			   
#include "sys.h"


// 延时相关函数     
void delay_init(void);
void delay_us(u32 nus);

// 1ms节拍相关函数
uint32_t delay_tick_inc(void);                                      // 每1ms调用一次，增加节拍计数
uint32_t delay_millis(void);                                        // 获取当前毫秒节拍计数  
bool delay_expired(uint32_t start_ms, uint32_t timeout_ms);         // 判断是否超时

// 阻塞式延时函数（不推荐在主循环中使用）
void delay_ms(u16 nms);

	
#endif





























