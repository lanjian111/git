#ifndef __FLAG_H
#define __FLAG_H

#include <stdint.h>                                              // 显式包含 uint8_t 类型定义，避免依赖外部包含顺序

/* GPIO 输出状态镜像，供状态机比较目标值并避免重复设置。 */
extern volatile uint8_t FLAG_CIRCULATION_PUMP;            //循环泵状态标志
extern volatile uint8_t FLAG_WATER_INLET_VALVE;           //进水电磁阀状态标志
extern volatile uint8_t FLAG_CLEANING_VALVE;              //清洗电磁阀状态标志
extern volatile uint8_t FLAG_CIRCULATION_THREE_WAY;       //循环三通阀状态标志
extern volatile uint8_t FLAG_LIQUID_OUTLET_THREE_WAY;     //出液三通阀状态标志
extern volatile uint8_t FLAG_WASH_START;                  // 清洗流程启动标志
extern volatile uint8_t FLAG_CIRCULATION_PUMP_ENABLE;     // 清洗流程内循环泵使能标志
extern volatile uint8_t FLAG_LIQUID_LEVEL_SAMPLE;         //液位传感器采样使能标志
extern volatile uint8_t FLAG_100MS;                       // SysTick 每 100 ms 置位，主循环消费后清零
extern volatile uint8_t FLAG_DEBUG_MODE;                 // 控制 LED 闪烁模式、GPIO 调试文本和 HMI 帧调试日志
extern volatile uint8_t FLAG_COFFEE_START;               // 萃取流程启动标志
extern volatile uint8_t FLAG_DRAIN_WASTE;                // 排废液流程启动标志
extern volatile uint8_t FLAG_DRAIN_BREW;                 // 排萃取液流程启动标志

/* 主循环轮询入口，有待处理的 100 ms 标志时执行一轮周期任务。 */
void FLAG_100MS_Execute(void);


void FLAG_100MS_Execute(void);




#endif

