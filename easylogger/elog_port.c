/*
 * EasyLogger port layer for bare-metal STM32F103RC + USART1 DMA
 *
 * Connects EasyLogger's output to USART_DMA_SendString().
 * Time source: system tick counter (ms).
 */

#include <elog.h> /* 取得日志级别、格式及输出入口的声明。 */
#include <elog_cfg.h> /* 取得日志缓冲区容量及编译开关配置。 */
#include <stdio.h> /* 取得 snprintf 等受缓冲区容量约束的格式化接口。 */
#include <string.h> /* 取得定长内存拷贝、填充、比较和字符串处理接口。 */
#include "stm32f10x.h" /* 使用 STM32F1 的寄存器、外设类型和基于工程配置启用的驱动接口。 */

/* 底层发送接口位于 USARTDMA.c，输出适配层忽略其返回状态。 */
extern void USART_DMA_SendString(const char *str); /* 声明接口，通过 USART1 发送以 NUL 结束的调试或日志字符串。 */

/* external: system millisecond counter (defined in delay.c) */
extern uint32_t delay_millis(void); /* 声明接口，读取系统累计运行毫秒数，供超时和周期任务计算时间差。 */

/*---------------------------------------------------------------------------*/
/* port init */
/*---------------------------------------------------------------------------*/
ElogErrCode elog_port_init(void) /* 初始化日志适配层，底层串口由主程序提前准备。 */
{
    /* USART1 DMA is already initialized in main() before elog_init() */
    return ELOG_NO_ERR; /* 底层 USART1 DMA 已在主程序中配置完毕，这里直接报告日志端口初始化成功。 */
}

/*---------------------------------------------------------------------------*/
/* port deinit */
/*---------------------------------------------------------------------------*/
void elog_port_deinit(void) /* 提供日志退出入口，适配层采用静态资源而无额外释放步骤。 */
{
    /* nothing to release in bare-metal */
}

/*---------------------------------------------------------------------------*/
/* output — this is the final output endpoint */
/*---------------------------------------------------------------------------*/
void elog_port_output(const char *log, size_t size) /* 把日志内容复制为有终止符的字符串，再交给串口发送。 */
{
    char buf[ELOG_LINE_BUF_SIZE + 2]; /* 分配 「buf[ELOG_LINE_BUF_SIZE + 2]」 数组，作为当前操作的数据缓冲区。 */

    if (size >= sizeof(buf)) /* 检查输入是否会占满本地缓冲区，必要时为字符串终止符留一个字节。 */
        size = sizeof(buf) - 1; /* 将输出截短到可用容量减一，预留位置写入终止符。 */

    memcpy(buf, log, size); /* 按长度复制原始数据，适用于包含零字节的缓冲区。 */
    buf[size] = '\0'; /* 将缓冲区 更新为 「'\0'」。 */

    /* 已启动的发送由 DMA 完成。若上一笔仍忙，底层会先忙等，可能超时。 */
    USART_DMA_SendString(buf); /* 通过 USART1 发送以 NUL 结束的调试或日志字符串。 */
}

/*---------------------------------------------------------------------------*/
/* 输出锁只屏蔽 USART1 TX DMA 完成中断，SysTick 和其他中断仍可执行。
 * 若加锁时上一笔 DMA 的完成尚未处理，tx_busy 可能持续为真，直到底层忙等超时。
 * 解锁会启用该 IRQ，本接口按单层调用使用。 */
/*---------------------------------------------------------------------------*/
void elog_port_output_lock(void) /* 屏蔽 USART1 TX DMA 完成中断，建立日志输出临界段。 */
{
    NVIC_DisableIRQ(DMA1_Channel4_IRQn); /* 屏蔽 DMA1_Channel4_IRQn 中断请求，控制何时进入中断服务。 */
}

void elog_port_output_unlock(void) /* 恢复 USART1 TX DMA 完成中断，结束日志输出临界段。 */
{
    NVIC_EnableIRQ(DMA1_Channel4_IRQn); /* 使能 DMA1_Channel4_IRQn 中断请求，控制何时进入中断服务。 */
}

/*---------------------------------------------------------------------------*/
/* 运行时间来自 SysTick 毫秒计数，和屏幕 RTC 独立。返回的静态字符缓冲区由下一次调用覆盖。 */
/*---------------------------------------------------------------------------*/
const char *elog_port_get_time(void) /* 用系统运行毫秒数生成日志时间戳。 */
{
    static char time_buf[20] = { 0 }; /* 定义时间 缓冲区，将时间 缓冲区 更新为 「{ 0 }」。 */
    uint32_t t = delay_millis(); /* 定义t，把当前毫秒节拍保存到 t，作为之后计算时间差的基准。 */
    uint32_t ms = t % 1000U; /* 取整秒之后的毫秒余数，用作时间戳小数部分。 */
    uint32_t s  = (t / 1000U) % 60U; /* 用毫秒数换算整秒，再取当前分钟内的秒数。 */
    uint32_t m  = (t / 60000U) % 60U; /* 用毫秒数换算整分钟，再取当前小时内的分钟数。 */
    uint32_t h  = t / 3600000U; /* 换算从开机起经过的完整小时数。 */

    snprintf(time_buf, sizeof(time_buf), "[%02lu:%02lu:%02lu.%03lu]", /* 用 「snprintf(time_buf, sizeof(time_buf), "[%02lu:%02lu:%02lu.%03lu]",」 补足上行的表达式、缓冲区内容或接口参数。 */
             (unsigned long)h, (unsigned long)m, (unsigned long)s, (unsigned long)ms); /* 按 long 无符号格式传入时分秒及毫秒，匹配时间戳的格式说明符。 */
    return time_buf; /* 返回本模块的静态文本缓冲区，供日志格式化立即使用。 */
}

/*---------------------------------------------------------------------------*/
/* process / thread info (bare-metal: empty) */
/*---------------------------------------------------------------------------*/
const char *elog_port_get_p_info(void) /* 提供进程信息字段，裸机工程返回空值。 */
{
    return ""; /* 将 「""」 返回给调用者，供上层使用。 */
}

const char *elog_port_get_t_info(void) /* 提供线程信息字段，裸机工程返回空值。 */
{
    return ""; /* 将 「""」 返回给调用者，供上层使用。 */
}
