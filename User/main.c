#include "stm32f10x.h" /* 使用 STM32F1 的寄存器、外设类型和基于工程配置启用的驱动接口。 */
#include "stm32f10x_it.h" /* 使中断服务函数的定义和工程声明保持一致。 */
#include "TEST.h" /* 取得板级测试任务的声明，供调度调用。 */
#include "flag.h" /* 取得采样、调试及流程启动标志的共享声明。 */
#include "delay.h" /* 使用 SysTick 毫秒计时和短时延时接口。 */
#include "LED.h" /* 取得指示灯初始化及状态控制接口。 */
#include "GPIO.h" /* 取得板级泵阀控制和液位采样接口。 */
#include "Key.h" /* 取得按键 GPIO 初始化和按键状态读取接口。 */
#include "USARTDMA.h" /* 取得日志与触摸屏串口的 DMA 收发接口。 */
#include "bw16_uart.h"   /* 板载 BW16 走 UART5，使用独立收发接口。 */
#include "hmi_driver.h" /* 取得触摸屏命令封装和链路处理接口。 */
#include "elog.h" /* 取得日志级别、格式及输出入口的声明。 */
#include "eeprom_cat24c256.h" /* 取得 CAT24C256 的容量、读写接口和状态定义。 */
#include "rtc_screen.h" /* 取得屏幕校时和本地时间缓存的公开接口。 */

#define LOG_TAG             "main" /* 指定日志标签 "main"，便于在串口输出中定位本模块。 */


/* 裸机主循环。中断负责毫秒节拍和串口传输，屏幕消息分发与工艺状态机在主循环中推进。 */
int main(void) /* 裸机程序入口，初始化板级资源后持续调度收包和周期任务。 */
{
    /* 先设中断优先级分组。delay_init() 必须在 EEPROM 总线第一次使用 delay_us() 前完成。 */
    NVIC_Configuration(); /* 设置 NVIC 优先级分组，使各外设的抢占和子优先级按同一规则解释。 */
    LED_Init(); /* 配置状态指示灯的 GPIO，供后续开关和闪烁任务控制。 */
    GPIO_Init_ALL(); /* 配置泵阀输出和液位输入，再初始化液位 ADC。 */
    Key_Init(); /* 配置按键输入，使软件可以读取实际按键电平。 */
    delay_init(); /* 配置 1 ms SysTick 和 DWT 周期计数器，为任务计时及 I2C 延时建立基准。 */
    EEPROM_Init(); /* 初始化软件 I2C 并将 EEPROM 异步写入状态设为空闲。 */
    USART_DMA_Init(115200);  /* USART1，PA9/PA10，日志和调试输出。 */
    USART2_DMA_Init(115200); /* HMI 串口实际上是 USART3，使用 PB10/PB11。 */
    /* 完成 NVIC 分组后启用 BW16 串口接收，让模块上电后的输出可由中断收进缓冲区。
     * 这里负责配置串口。模块就绪检查、AT 指令和应答处理由上层任务接入。
     */
    BW16_UART_Init(BW16_UART_DEFAULT_BAUD); /* 按指定波特率初始化与 BW16 相连的 UART5，并开启中断接收。 */
    HMI_LinkInit(); /* 初始化 HMI 接收链路，清理历史接收状态并挂接回调。 */

    /* 日志端口复用已初始化的 USART1 DMA。日志时间戳来自系统运行毫秒数。 */
    elog_init(); /* 初始化日志状态和输出端口。 */
    elog_set_fmt(ELOG_LVL_ERROR,  ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME); /* 配置指定日志级别要显示的格式字段。 */
    elog_set_fmt(ELOG_LVL_INFO,   ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME); /* 配置指定日志级别要显示的格式字段。 */
    elog_start(); /* 开启日志输出，使后续日志可以送往已配置端口。 */
    log_i("System boot"); /* 以信息级别记录 「"System boot"」，便于从日志跟踪当前分支。 */

    while (1) /* 持续轮询设备任务，使裸机程序在初始化后常驻运行。 */
    {
        HMI_LinkTask(); /* 轮询屏幕 DMA 并取出完整帧，在主循环上下文分发。 */
        /* 先让 HMI 任务分发已收到的时间回包，再推进本地时钟和同步状态。
         * RTC 任务每轮检查一次即可，等待回包时立即返回，周期性设备控制可继续运行。
         */
        RTC_Screen_Task(); /* 推进本地时间并检查校时调度，等待屏幕回包期间保持周期任务可运行。 */
        FLAG_100MS_Execute(); /* 消费待处理的 100 ms 标志，依次运行采样和业务状态机。 */
        /* 周期任务可能发送日志或执行软件 I2C，结束后再次及时抽取屏幕 DMA 数据。 */
        HMI_LinkTask(); /* 轮询屏幕 DMA 并取出完整帧，在主循环上下文分发。 */
    }
}


