#include "stm32f10x.h"
#include "stm32f10x_it.h"
#include "TEST.h"
#include "flag.h"
#include "delay.h"
#include "LED.h"
#include "GPIO.h"
#include "Key.h"
#include "USARTDMA.h"
#include "hmi_driver.h"
#include "elog.h"
#include "eeprom_cat24c256.h"
#include "rtc_storage.h"
#include "rtc_mcu.h"
#include "cmd_process.h"

#define LOG_TAG             "main"


int main(void)
{
    NVIC_Configuration();
    LED_Init();
    GPIO_Init_ALL();
    Key_Init();
    delay_init();
    EEPROM_Init();
    USART_DMA_Init(115200);
    USART2_DMA_Init(115200);
    HMI_LinkInit();
    USART2_GPIO_Repair();

    elog_init();
    elog_set_fmt(ELOG_LVL_ERROR,  ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_set_fmt(ELOG_LVL_INFO,   ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_start();
    log_i("System boot");

    RTC_MCU_Init();
    RTC_BootRestore();

    while (1)
    {
        HMI_LinkTask();
        if (FLAG_1S) { FLAG_1S = 0; RTC_RefreshIfMinuteChanged(); }
        FLAG_100MS_Execute();
        HMI_LinkTask();
    }
}


