#include "stm32f10x.h"
#include "stm32f10x_it.h"
#include "TEST.h"
#include "flag.h"
#include "delay.h"
#include "LED.h"
#include "GPIO.h"
#include "Key.h"
#include "USARTDMA.h"
#include "bw16_uart.h"   /* 板载 BW16 走 UART5，使用独立收发接口。 */
#include "hmi_driver.h"
#include "elog.h"
#include "eeprom_cat24c256.h"
#include "rtc_screen.h"

#define LOG_TAG             "main"


/* 裸机主循环。中断负责毫秒节拍和串口传输，屏幕消息分发与工艺状态机在主循环中推进。 */
int main(void)
{
    /* 先设中断优先级分组。delay_init() 必须在 EEPROM 总线第一次使用 delay_us() 前完成。 */
    NVIC_Configuration();
    LED_Init();
    GPIO_Init_ALL();
    Key_Init();
    delay_init();
    EEPROM_Init();
    USART_DMA_Init(115200);  /* USART1，PA9/PA10，日志和调试输出。 */
    USART2_DMA_Init(115200); /* HMI 串口实际上是 USART3，使用 PB10/PB11。 */
    /* 完成 NVIC 分组后启用 BW16 串口接收，让模块上电后的输出可由中断收进缓冲区。
     * 这里负责配置串口。模块就绪检查、AT 指令和应答处理由上层任务接入。
     */
    BW16_UART_Init(BW16_UART_DEFAULT_BAUD);
    HMI_LinkInit();

    /* 日志端口复用已初始化的 USART1 DMA。日志时间戳来自系统运行毫秒数。 */
    elog_init();
    elog_set_fmt(ELOG_LVL_ERROR,  ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_set_fmt(ELOG_LVL_INFO,   ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_start();
    log_i("System boot");

    while (1)
    {
        HMI_LinkTask();
        /* 先让 HMI 任务分发已收到的时间回包，再推进本地时钟和同步状态。
         * RTC 任务每轮检查一次即可，等待回包时立即返回，周期性设备控制可继续运行。
         */
        RTC_Screen_Task();
        FLAG_100MS_Execute();
        /* 周期任务可能发送日志或执行软件 I2C，结束后再次及时抽取屏幕 DMA 数据。 */
        HMI_LinkTask();
    }
}


