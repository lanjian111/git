#include "stm32f10x.h"   
#include "LED.h"
#include "TEST.h"
#include "GPIO.h"
#include "wash.h"
#include "coffemake.h"
#include "eeprom_cat24c256.h"

/* 执行器标志由 GPIO 设置函数同步更新，表示软件写出的状态。
 * 流程标志由屏幕回调或其他状态机置位，推进和清理由各任务负责。
 */
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

/* 消费 SysTick 置位的单比特标志。多次到期可合并成一次执行，没有补跑机制。
 * 先更新 ADC 缓存，再依次运行清洗、萃取、排废液和排萃取液，
 * 因此前面的任务置位排液标志后，后面的排液任务可在这一轮响应。
 */
void FLAG_100MS_Execute(void)
{
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

        FLAG_100MS = 0;
    }
}

