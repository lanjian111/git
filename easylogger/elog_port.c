/*
 * EasyLogger port layer for bare-metal STM32F103RC + USART3 DMA
 *
 * Connects EasyLogger's output to USART3_DMA_SendString().
 * Time source: system tick counter (ms).
 */

#include <elog.h>
#include <elog_cfg.h>
#include <stdio.h>
#include <string.h>
#include "stm32f10x.h"

/* external: USART3 DMA send (defined in USARTDMA.c) */
extern void USART3_DMA_SendString(const char *str);

/* external: system millisecond counter (defined in delay.c) */
extern uint32_t delay_millis(void);

/*---------------------------------------------------------------------------*/
/* port init */
/*---------------------------------------------------------------------------*/
ElogErrCode elog_port_init(void)
{
    /* USART3 DMA is already initialized in main() before elog_init() */
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

    /* send via USART3 DMA (non-blocking async) */
    USART3_DMA_SendString(buf);
}

/*---------------------------------------------------------------------------*/
/* output lock / unlock                                                       */
/* Only disable USART3 TX DMA interrupt (DMA1_Channel2) to protect husart3   */
/* from concurrent access by the TX-complete ISR.                             */
/* SysTick and all other interrupts keep running — no time drift.             */
/*---------------------------------------------------------------------------*/
void elog_port_output_lock(void)
{
    NVIC_DisableIRQ(DMA1_Channel2_IRQn);
}

void elog_port_output_unlock(void)
{
    NVIC_EnableIRQ(DMA1_Channel2_IRQn);
}

/*---------------------------------------------------------------------------*/
/* time — returns formatted timestamp from system tick */
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
