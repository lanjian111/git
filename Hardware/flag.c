#include "stm32f10x.h"   
#include "LED.h"
#include "TEST.h"
#include "GPIO.h"
#include "wash.h"
#include "coffemake.h"
#include "eeprom_cat24c256.h"

// 状态标志定义
volatile uint8_t FLAG_CIRCULATION_PUMP = 0;            //循环泵状态标志
volatile uint8_t FLAG_WATER_INLET_VALVE = 0;           //进水电磁阀状态标志
volatile uint8_t FLAG_CLEANING_VALVE = 0;              //清洗电磁阀状态标志
volatile uint8_t FLAG_CIRCULATION_THREE_WAY = 0;       //循环三通阀状态标志
volatile uint8_t FLAG_LIQUID_OUTLET_THREE_WAY = 0;     //出液三通阀状态标志
volatile uint8_t FLAG_WASH_START = 0;                  // 清洗流程启动标志
volatile uint8_t FLAG_CIRCULATION_PUMP_ENABLE = 0;     // 清洗流程内循环泵使能标志
volatile uint8_t FLAG_LIQUID_LEVEL_SAMPLE = 0;         //液位传感器采样使能标志
volatile uint8_t FLAG_100MS = 0;                       // 100ms时间标志
volatile uint8_t FLAG_DEBUG_MODE = 1;                  // 调试模式标志 (0=正常, 1=调试，默认开启)
volatile uint8_t FLAG_COFFEE_START = 0;                // 萃取流程启动标志
volatile uint8_t FLAG_DRAIN_WASTE = 0;                 // 排废液流程启动标志
volatile uint8_t FLAG_DRAIN_BREW = 0;                  // 排萃取液流程启动标志
volatile uint8_t FLAG_1S = 0;                          // 1秒时间标志

void FLAG_100MS_Execute(void)
{
    static uint8_t tick_100ms = 0;

    if (FLAG_100MS == 1)
    {
        LED_flicker();
        TEST_Function();
        Liquid_Level_Update();
        Wash_Task();
        Coffee_Task();
        Coffee_DrainWaste_Task();
        Coffee_DrainBrew_Task();
        EEPROM_Task();

        /* 每 10 次 (1秒) 置位 FLAG_1S */
        tick_100ms++;
        if (tick_100ms >= 10)
        {
            tick_100ms = 0;
            FLAG_1S = 1;
        }

        FLAG_100MS = 0;
    }
}

