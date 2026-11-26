#include "stm32f10x.h"    /* 使用 STM32F1 的寄存器、外设类型和基于工程配置启用的驱动接口。 */
#include "LED.h" /* 取得指示灯初始化及状态控制接口。 */
#include "TEST.h" /* 取得板级测试任务的声明，供调度调用。 */
#include "GPIO.h" /* 取得板级泵阀控制和液位采样接口。 */
#include "wash.h" /* 取得清洗流程的控制和周期推进接口。 */
#include "coffemake.h" /* 取得萃取及排液流程的控制接口。 */
#include "eeprom_cat24c256.h" /* 取得 CAT24C256 的容量、读写接口和状态定义。 */

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
void FLAG_100MS_Execute(void) /* 消费待处理的 100 ms 标志，依次运行采样和业务状态机。 */
{
    if (FLAG_100MS == 1) /* 只有 SysTick 留下待处理节拍时才运行周期任务。 */
    {
        LED_flicker(); /* 推进状态灯闪烁，用于显示周期运行和调试状态。 */
        TEST_Function(); /* 执行当前编译分支下的板级测试。 */
        Liquid_Level_Update(); /* 按需启动或停止液位 ADC，完成一次转换后刷新缓存。 */
        Wash_Task(); /* 根据当前状态推进两段清洗，每次调用执行本轮分支。 */
        Coffee_Task(); /* 按当前液位和计时结果推进萃取状态机。 */
        Coffee_DrainWaste_Task(); /* 推进排废液状态机，按液位条件和超时控制阀泵。 */
        Coffee_DrainBrew_Task(); /* 推进排萃取液状态机，按液位条件和超时控制阀泵。 */
        EEPROM_Task(); /* 推进异步写入的等待、拆页和完成状态，避免忙等内部写入周期。 */

        FLAG_100MS = 0; /* 本轮设备控制和 EEPROM 任务结束后消费节拍，等待下一次置位。 */
    }
}

