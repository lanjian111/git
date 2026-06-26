#ifndef __FLAG_H
#define __FLAG_H

#include <stdint.h>                                              // 显式包含 uint8_t 类型定义，避免依赖外部包含顺序

extern volatile uint8_t FLAG_CIRCULATION_PUMP;            //循环泵状态标志
extern volatile uint8_t FLAG_WATER_INLET_VALVE;           //进水电磁阀状态标志
extern volatile uint8_t FLAG_CLEANING_VALVE;              //清洗电磁阀状态标志
extern volatile uint8_t FLAG_CIRCULATION_THREE_WAY;       //循环三通阀状态标志
extern volatile uint8_t FLAG_LIQUID_OUTLET_THREE_WAY;     //出液三通阀状态标志
extern volatile uint8_t FLAG_WASH_START;                  // 清洗流程启动标志
extern volatile uint8_t FLAG_CIRCULATION_PUMP_ENABLE;     // 清洗流程内循环泵使能标志
extern volatile uint8_t FLAG_LIQUID_LEVEL_SAMPLE;         //液位传感器采样使能标志
extern volatile uint8_t FLAG_100MS;                       // 100ms时间标志
extern volatile uint8_t FLAG_DEBUG_MODE;                 // 调试模式标志


void FLAG_100MS_Execute(void);




#endif

