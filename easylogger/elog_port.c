/*
 * EasyLogger port layer for bare-metal STM32F103RC + USART1 DMA
 *
 * Connects EasyLogger's output to USART_DMA_SendString().
 * Time source: system tick counter (ms).
 */

#include <elog.h>
#include <elog_cfg.h>
#include <stdio.h>
#include <string.h>
#include "stm32f10x.h"

/* 底层发送接口位于 USARTDMA.c，输出适配层忽略其返回状态。 */
extern void USART_DMA_SendString(const char *str);

/* external: system millisecond counter (defined in delay.c) */
extern uint32_t delay_millis(void);

/*---------------------------------------------------------------------------*/
/* port init */
/*---------------------------------------------------------------------------*/
ElogErrCode elog_port_init(void)
{
    /* USART1 DMA is already initialized in main() before elog_init() */
    return ELOG_NO_ERR;
}

/*---------------------------------------------------------------------------*/
/* port deinit */
/*---------------------------------------------------------------------------*/
void elog_port_deinit(void)
{
    /* nothing to release in bare-metal */
}

/*---------------------------------------------------------------------------*/
/* output — this is the final output endpoint */
/*---------------------------------------------------------------------------*/
void elog_port_output(const char *log, size_t size)
{
    char buf[ELOG_LINE_BUF_SIZE + 2];

    if (size >= sizeof(buf))
        size = sizeof(buf) - 1;

    memcpy(buf, log, size);
    buf[size] = '\0';

    /* 已启动的发送由 DMA 完成。若上一笔仍忙，底层会先忙等，可能超时。 */
    USART_DMA_SendString(buf);
}

/*---------------------------------------------------------------------------*/
/* 输出锁只屏蔽 USART1 TX DMA 完成中断，SysTick 和其他中断仍可执行。
 * 若加锁时上一笔 DMA 的完成尚未处理，tx_busy 可能持续为真，直到底层忙等超时。
 * 解锁会启用该 IRQ，本接口按单层调用使用。 */
/*---------------------------------------------------------------------------*/
void elog_port_output_lock(void)
{
    NVIC_DisableIRQ(DMA1_Channel4_IRQn);
}

void elog_port_output_unlock(void)
{
    NVIC_EnableIRQ(DMA1_Channel4_IRQn);
}

/*---------------------------------------------------------------------------*/
/* 运行时间来自 SysTick 毫秒计数，和屏幕 RTC 独立。返回的静态字符缓冲区由下一次调用覆盖。 */
/*---------------------------------------------------------------------------*/
const char *elog_port_get_time(void)
{
    static char time_buf[20] = { 0 };
    uint32_t t = delay_millis();
    uint32_t ms = t % 1000U;
    uint32_t s  = (t / 1000U) % 60U;
    uint32_t m  = (t / 60000U) % 60U;
    uint32_t h  = t / 3600000U;

    snprintf(time_buf, sizeof(time_buf), "[%02lu:%02lu:%02lu.%03lu]",
             (unsigned long)h, (unsigned long)m, (unsigned long)s, (unsigned long)ms);
    return time_buf;
}

/*---------------------------------------------------------------------------*/
/* process / thread info (bare-metal: empty) */
/*---------------------------------------------------------------------------*/
const char *elog_port_get_p_info(void)
{
    return "";
}

const char *elog_port_get_t_info(void)
{
    return "";
}
