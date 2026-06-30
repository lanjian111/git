#include "eeprom_cat24c256.h"
#include "soft_i2c.h"
#include "delay.h"

/*==========================================================================
 * CAT24C256 EEPROM 驱动实现
 *
 * 异步写入状态机:
 *   IDLE → SEND_PAGE → WAIT_WRITE → (有下一页?) → SEND_PAGE
 *                                      ↓ 无下一页
 *                                    DONE → IDLE
 *
 * 关键点:
 *   - SEND_PAGE 阶段使用阻塞式 I?C (约 500μs)，可接受
 *   - WAIT_WRITE 阶段使用 delay_expired() 非阻塞等待 5ms
 *   - WriteBuffer 自动拆页，不跨页边界
 *==========================================================================*/

/* --- 写入控制块 (异步写入上下文) --- */
typedef struct {
    uint16_t       total_addr;            /* 总起始地址                          */
    const uint8_t *total_buf;            /* 总数据缓冲区指针                     */
    uint16_t       total_len;            /* 总长度                              */
    uint16_t       done_len;             /* 已完成长度                          */

    uint16_t       page_addr;            /* 当前页起始地址                      */
    uint16_t       page_len;             /* 当前页剩余要写长度                   */
    const uint8_t *page_buf;            /* 当前页数据指针                       */

    uint32_t       wait_start_ms;        /* 5ms 等待起始时间戳                   */
} EEPROM_WriteCtx_t;

/* --- 静态变量 --- */
static EEPROM_WriteCtx_t g_write_ctx;              /* 写入控制块                 */
static EEPROM_State_t    g_state = EEPROM_STATE_IDLE;  /* 当前状态           */
static uint8_t           g_single_byte_buf;        /* 单字节写入临时缓冲区        */
static uint8_t           g_last_read_nack = 0;     /* 上次读取是否遇到 NACK      */

/*==========================================================================
 * 内部函数声明
 *==========================================================================*/
static void     _prepare_next_page(void);                    /* 准备下一页数据          */
static void     _start_page_write(void);                     /* 发起当前页写入          */

/*==========================================================================
 * EEPROM_Init - 初始化
 *==========================================================================*/
void EEPROM_Init(void)
{
    SoftI2C_Init();
    g_state = EEPROM_STATE_IDLE;
}

/*==========================================================================
 * EEPROM_ReadByte - 同步读取单字节
 *
 * I?C 事务: START → 设备地址(W) → 地址高8位 → 地址低8位
 *                → REPEAT START → 设备地址(R) → 读1字节(NACK) → STOP
 *==========================================================================*/
uint8_t EEPROM_ReadByte(uint16_t addr)
{
    uint8_t data, ack;

    g_last_read_nack = 0;
    addr &= CAT24C256_ADDR_MASK;

    SoftI2C_Start();
    ack  = SoftI2C_SendByte((CAT24C256_ADDR << 1) | 0x00);     /* 设备地址 + 写 */
    ack |= SoftI2C_SendByte((uint8_t)(addr >> 8));              /* 存储器地址高 8 位 */
    ack |= SoftI2C_SendByte((uint8_t)(addr & 0xFF));            /* 存储器地址低 8 位 */

    SoftI2C_Start();                                             /* 重复起始 */
    ack |= SoftI2C_SendByte((CAT24C256_ADDR << 1) | 0x01);     /* 设备地址 + 读 */

    if (ack)
    {
        g_last_read_nack = 1;
        SoftI2C_Stop();
        return 0xFF;
    }

    data = SoftI2C_ReadByte(1);                                  /* 读 1 字节，发 NACK */
    SoftI2C_Stop();

    return data;
}

/*==========================================================================
 * EEPROM_ReadBuffer - 同步连续读取
 *
 * CAT24C256 支持连续读取，内部地址自动递增，跨页自动回卷。
 * 最后一个字节发 NACK 终止传输。
 *==========================================================================*/
void EEPROM_ReadBuffer(uint16_t addr, uint8_t *buf, uint16_t len)
{
    uint16_t i;
    uint8_t  ack;

    if (len == 0) return;
    g_last_read_nack = 0;
    addr &= CAT24C256_ADDR_MASK;

    /* 伪写: 设置内部地址指针 */
    SoftI2C_Start();
    ack  = SoftI2C_SendByte((CAT24C256_ADDR << 1) | 0x00);
    ack |= SoftI2C_SendByte((uint8_t)(addr >> 8));
    ack |= SoftI2C_SendByte((uint8_t)(addr & 0xFF));

    /* 重复起始 + 读 */
    SoftI2C_Start();
    ack |= SoftI2C_SendByte((CAT24C256_ADDR << 1) | 0x01);

    if (ack)
    {
        g_last_read_nack = 1;
        SoftI2C_Stop();
        return;                                              /* NACK，提前终止 */
    }

    for (i = 0; i < len; i++)
    {
        if (i == (len - 1))
            buf[i] = SoftI2C_ReadByte(1);                    /* 最后一个字节: NACK */
        else
            buf[i] = SoftI2C_ReadByte(0);                    /* 继续读: ACK */
    }

    SoftI2C_Stop();
}

/*==========================================================================
 * EEPROM_WriteByte - 异步写入单字节
 *==========================================================================*/
EEPROM_Result_t EEPROM_WriteByte(uint16_t addr, uint8_t data)
{
    if (!EEPROM_IsIdle()) return EEPROM_BUSY;
    if (addr > CAT24C256_ADDR_MASK) return EEPROM_ERROR;

    g_single_byte_buf      = data;            /* 复制到静态缓冲区，避免栈指针悬空 */
    g_write_ctx.total_addr = addr;
    g_write_ctx.total_buf  = &g_single_byte_buf;
    g_write_ctx.total_len  = 1;
    g_write_ctx.done_len   = 0;

    _prepare_next_page();
    _start_page_write();

    return EEPROM_OK;
}

/*==========================================================================
 * EEPROM_WritePage - 异步写入一页 (len ≤ 64，不跨页)
 *
 * 注意: buf 指针在异步操作完成之前必须保持有效！
 *==========================================================================*/
EEPROM_Result_t EEPROM_WritePage(uint16_t addr, const uint8_t *buf, uint16_t len)
{
    if (!EEPROM_IsIdle()) return EEPROM_BUSY;
    if (addr > CAT24C256_ADDR_MASK) return EEPROM_ERROR;
    if (len == 0 || len > CAT24C256_PAGE_SIZE) return EEPROM_ERROR;

    /* 检查是否跨页 */
    uint16_t page_start  = addr & ~(CAT24C256_PAGE_SIZE - 1);
    uint16_t page_end    = page_start + CAT24C256_PAGE_SIZE - 1;
    if ((addr + len - 1) > page_end) return EEPROM_ERROR;  /* 不允许跨页 */

    g_write_ctx.total_addr = addr;
    g_write_ctx.total_buf  = buf;
    g_write_ctx.total_len  = len;
    g_write_ctx.done_len   = 0;

    _prepare_next_page();
    _start_page_write();

    return EEPROM_OK;
}

/*==========================================================================
 * EEPROM_WriteBuffer - 异步写入任意长度 (自动拆页)
 *
 * 内部自动按 64 字节页边界拆分，每页写完等待 5ms 再写下一页。
 *
 * 注意: buf 指针在异步操作完成之前必须保持有效！
 *==========================================================================*/
EEPROM_Result_t EEPROM_WriteBuffer(uint16_t addr, const uint8_t *buf, uint16_t len)
{
    if (!EEPROM_IsIdle()) return EEPROM_BUSY;
    if (addr > CAT24C256_ADDR_MASK) return EEPROM_ERROR;
    if (len == 0) return EEPROM_ERROR;
    if ((uint32_t)addr + len > CAT24C256_CAPACITY) return EEPROM_ERROR;

    g_write_ctx.total_addr = addr;
    g_write_ctx.total_buf  = buf;
    g_write_ctx.total_len  = len;
    g_write_ctx.done_len   = 0;

    _prepare_next_page();
    _start_page_write();

    return EEPROM_OK;
}

/*==========================================================================
 * EEPROM_Task - 状态机推进函数 (每 100ms 调用)
 *==========================================================================*/
void EEPROM_Task(void)
{
    switch (g_state)
    {
    case EEPROM_STATE_IDLE:
    case EEPROM_STATE_DONE:
    case EEPROM_STATE_NACK:
        /* 空闲、完成、错误状态: 等待上层处理 */
        break;

    case EEPROM_STATE_SEND_PAGE:
        /* 本页已在 _start_page_write 中完成，直接进入等待 */
        g_state = EEPROM_STATE_WAIT_WRITE;
        g_write_ctx.wait_start_ms = delay_millis();   /* 记录 5ms 等待起点 */
        break;

    case EEPROM_STATE_WAIT_WRITE:
        /* 非阻塞等待 5ms EEPROM 内部写入周期 */
        if (delay_expired(g_write_ctx.wait_start_ms, CAT24C256_WRITE_CYCLE_MS))
        {
            /* 5ms 到，更新完成计数 */
            g_write_ctx.done_len += g_write_ctx.page_len;

            /* 检查是否还有数据要写 */
            if (g_write_ctx.done_len < g_write_ctx.total_len)
            {
                _prepare_next_page();       /* 准备下一页 */
                _start_page_write();        /* 发下一页 */
                /* 下一次 EEPROM_Task 调用时进入 SEND_PAGE → WAIT_WRITE */
            }
            else
            {
                g_state = EEPROM_STATE_DONE;  /* 全部完成 */
            }
        }
        break;
    }
}

/*==========================================================================
 * EEPROM_GetState - 查询当前状态
 *==========================================================================*/
EEPROM_State_t EEPROM_GetState(void)
{
    return g_state;
}

/*==========================================================================
 * EEPROM_IsIdle - 是否空闲
 *==========================================================================*/
uint8_t EEPROM_IsIdle(void)
{
    return (g_state == EEPROM_STATE_IDLE) ? 1 : 0;
}

/*==========================================================================
 * EEPROM_ClearState - 清除 DONE/NACK 状态回到 IDLE
 *==========================================================================*/
void EEPROM_ClearState(void)
{
    if (g_state == EEPROM_STATE_DONE || g_state == EEPROM_STATE_NACK)
    {
        g_state = EEPROM_STATE_IDLE;
    }
}

/*==========================================================================
 * _prepare_next_page - 计算下一页的地址和长度
 *
 * 根据已完成的 done_len，计算下一页的起始地址和长度。
 * 自动处理页边界对齐。
 *==========================================================================*/
static void _prepare_next_page(void)
{
    uint16_t current_addr;       /* 本页起始地址         */
    uint16_t page_remain;        /* 当前页剩余空间        */
    uint16_t remain_len;         /* 总共还剩多少要写     */

    current_addr = g_write_ctx.total_addr + g_write_ctx.done_len;
    remain_len   = g_write_ctx.total_len  - g_write_ctx.done_len;

    /* 当前页剩余空间 */
    page_remain = CAT24C256_PAGE_SIZE - (current_addr % CAT24C256_PAGE_SIZE);

    /* 本页写入长度 = min(页剩余空间, 总剩余长度) */
    g_write_ctx.page_len = (remain_len < page_remain) ? remain_len : page_remain;
    g_write_ctx.page_addr = current_addr;
    g_write_ctx.page_buf  = g_write_ctx.total_buf + g_write_ctx.done_len;
}

/*==========================================================================
 * _start_page_write - 发起一页的 I?C 写入 (阻塞式, ~500μs)
 *
 * I?C 事务: START → 设备地址(W) → 地址高8位 → 地址低8位
 *                → 数据[0] → 数据[1] → ... → STOP
 *
 * 进来前确保: g_write_ctx.page_addr / page_buf / page_len 已就绪
 * 发送完成后: 状态切换到 SEND_PAGE，等待 EEPROM_Task 推进
 *
 * NACK 处理: 如果设备无应答，进入 NACK 错误状态
 *==========================================================================*/
static void _start_page_write(void)
{
    uint16_t i;
    uint8_t  ack;

    SoftI2C_Start();

    /* 设备地址 + 写 */
    ack = SoftI2C_SendByte((CAT24C256_ADDR << 1) | 0x00);
    if (ack)
    {
        SoftI2C_Stop();
        g_state = EEPROM_STATE_NACK;
        return;
    }

    /* 存储器地址高 8 位 */
    ack = SoftI2C_SendByte((uint8_t)(g_write_ctx.page_addr >> 8));
    if (ack)
    {
        SoftI2C_Stop();
        g_state = EEPROM_STATE_NACK;
        return;
    }

    /* 存储器地址低 8 位 */
    ack = SoftI2C_SendByte((uint8_t)(g_write_ctx.page_addr & 0xFF));
    if (ack)
    {
        SoftI2C_Stop();
        g_state = EEPROM_STATE_NACK;
        return;
    }

    /* 发送数据字节 */
    for (i = 0; i < g_write_ctx.page_len; i++)
    {
        ack = SoftI2C_SendByte(g_write_ctx.page_buf[i]);
        if (ack)
        {
            SoftI2C_Stop();
            g_state = EEPROM_STATE_NACK;
            return;
        }
    }

    SoftI2C_Stop();
    g_state = EEPROM_STATE_SEND_PAGE;
}

/*==========================================================================
 * EEPROM_ReadHadNack - 查询上次读取是否遇到 NACK
 *
 * 返回: 0 = 读取正常,  1 = 上次读取遇到 NACK (设备未应答)
 *==========================================================================*/
uint8_t EEPROM_ReadHadNack(void)
{
    return g_last_read_nack;
}
