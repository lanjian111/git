#include "stm32f10x.h"
#include "stm32f10x_it.h"
#include "TEST.h"
#include "flag.h"
#include "delay.h"
#include "LED.h"
#include "GPIO.h"
#include "Key.h"                                                  // 接入按键模块
#include "USARTDMA.h"
#include "hmi_driver.h"


int main(void)
{
    NVIC_Configuration(); // 配置中断优先级分组
    LED_Init();           // 初始化LED
    GPIO_Init_ALL();      // 初始化所有GPIO
    Key_Init();           // 初始化按键（PB1/PB11）
    delay_init();         // 初始化延迟函数（1ms节拍）
    USART_DMA_Init(115200); // 初始化USART DMA，波特率115200
    HMI_LinkInit();       // 绑定HMI接收回调与解析链路


    while (1)
    {
        HMI_LinkTask();       // 高频轮询：搬运串口数据、拼完整帧并分发消息

        /* 按键处理：长按 Key1(PC2) 2秒切换调试模式 */
        {
            uint8_t keys = Key_GetNum();
            if (keys & KEY1_LONG_FLAG)
            {
                FLAG_DEBUG_MODE = !FLAG_DEBUG_MODE;               // 翻转调试模式
            }
        }

        FLAG_100MS_Execute();
    }
}
