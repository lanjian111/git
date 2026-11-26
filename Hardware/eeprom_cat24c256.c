#include "eeprom_cat24c256.h" /* 取得 CAT24C256 的容量、读写接口和状态定义。 */
#include "soft_i2c.h" /* 取得模拟 I2C 的起停、发送与接收接口。 */
#include "delay.h" /* 使用 SysTick 毫秒计时和短时延时接口。 */

/*==========================================================================
 * CAT24C256 EEPROM 驱动实现
 *
 * 异步写入状态机:
 *   写接口同步发送第一页，成功后标记 SEND_PAGE，任一字节 NACK 则进入 NACK。
 *   任务看到 SEND_PAGE 后开始计时并转 WAIT_WRITE，至少等待 5 ms 再同步发下一页或转 DONE。
 *   实际检查间隔由 EEPROM_Task 调用周期决定。DONE 和 NACK 均须由上层清除才回到 IDLE。
 *==========================================================================*/

/* --- 写入控制块 (异步写入上下文) --- */
typedef struct { /* 定义关联字段的结构类型，供相关接口复用。 */
    uint16_t       total_addr;            /* 总起始地址                          */
    const uint8_t *total_buf;            /* 总数据缓冲区指针                     */
    uint16_t       total_len;            /* 总长度                              */
    uint16_t       done_len;             /* 已完成长度                          */

    uint16_t       page_addr;            /* 当前页起始地址                      */
    uint16_t       page_len;             /* 当前页剩余要写长度                   */
    const uint8_t *page_buf;            /* 当前页数据指针                       */

    uint32_t       wait_start_ms;        /* 5ms 等待起始时间戳                   */
} EEPROM_WriteCtx_t; /* 为上述定义命名为 EEPROM_WriteCtx_t，便于在声明中引用。 */

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
void EEPROM_Init(void) /* 初始化软件 I2C 并将 EEPROM 异步写入状态设为空闲。 */
{
    SoftI2C_Init(); /* 初始化软件 I2C 的 SDA 和 SCL 引脚。 */
    g_state = EEPROM_STATE_IDLE; /* 切换到 EEPROM_STATE_IDLE，下一次状态分发按该状态继续。 */
}

/*==========================================================================
 * EEPROM_ReadByte - 同步读取单字节
 *
 * I2C 事务: START → 设备地址(W) → 地址高8位 → 地址低8位
 *                → REPEAT START → 设备地址(R) → 读1字节(NACK) → STOP
 *==========================================================================*/
uint8_t EEPROM_ReadByte(uint16_t addr) /* EEPROM 读取 字节。 */
{
    uint8_t data, ack; /* 声明 「data, ack」，用于本函数的中间结果和循环控制。 */

    g_last_read_nack = 0; /* 将最近一次 EEPROM 读取的无应答标志 更新为 「0」。 */
    addr &= CAT24C256_ADDR_MASK; /* 将地址 按位保留掩码 「CAT24C256_ADDR_MASK」。 */

    SoftI2C_Start(); /* 在 SCL 为高期间拉低 SDA，形成起始或重复起始条件。 */
    ack  = SoftI2C_SendByte((CAT24C256_ADDR << 1) | 0x00);     /* 设备地址 + 写 */
    ack |= SoftI2C_SendByte((uint8_t)(addr >> 8));              /* 存储器地址高 8 位 */
    ack |= SoftI2C_SendByte((uint8_t)(addr & 0xFF));            /* 存储器地址低 8 位 */

    SoftI2C_Start();                                             /* 重复起始 */
    ack |= SoftI2C_SendByte((CAT24C256_ADDR << 1) | 0x01);     /* 设备地址 + 读 */

    if (ack) /* 应答结果表明有 NACK 时进入错误分支，结束当前 I2C 事务。 */
    {
        g_last_read_nack = 1; /* 将最近一次 EEPROM 读取的无应答标志 更新为 「1」。 */
        SoftI2C_Stop(); /* 在 SCL 为高期间释放 SDA，结束本次总线事务。 */
        return 0xFF; /* 以 0xFF 返回本分支的结果，调用方据此处理当前状态。 */
    }

    data = SoftI2C_ReadByte(1);                                  /* 读 1 字节，发 NACK */
    SoftI2C_Stop(); /* 在 SCL 为高期间释放 SDA，结束本次总线事务。 */

    return data; /* 将 「data」 返回给调用者，供上层使用。 */
}

/*==========================================================================
 * EEPROM_ReadBuffer - 同步连续读取
 *
 * 连续读取时，地址逐字节递增，可跨 64 字节页，到芯片末尾才回卷。
 * 最后一字节由主机发送 NACK。地址阶段收到 NACK 时立即返回，保留原输出缓冲区。
 *==========================================================================*/
void EEPROM_ReadBuffer(uint16_t addr, uint8_t *buf, uint16_t len) /* EEPROM 读取 缓冲区。 */
{
    uint16_t i; /* 声明遍历计数器，用来逐个处理指定范围内的元素。 */
    uint8_t  ack; /* 声明从设备的应答结果，为后续计算或读取保存中间值。 */

    if (len == 0) return; /* 参数为空或长度为零时提前返回，避免进行无效读写。 */
    g_last_read_nack = 0; /* 将最近一次 EEPROM 读取的无应答标志 更新为 「0」。 */
    addr &= CAT24C256_ADDR_MASK; /* 将地址 按位保留掩码 「CAT24C256_ADDR_MASK」。 */

    /* 伪写: 设置内部地址指针 */
    SoftI2C_Start(); /* 在 SCL 为高期间拉低 SDA，形成起始或重复起始条件。 */
    ack  = SoftI2C_SendByte((CAT24C256_ADDR << 1) | 0x00); /* 发送带写位的设备地址，选择 EEPROM 并开始传输，保存设备应答结果。 */
    ack |= SoftI2C_SendByte((uint8_t)(addr >> 8)); /* 发送存储地址高字节，先指定地址的高位，将无应答并入此前结果。 */
    ack |= SoftI2C_SendByte((uint8_t)(addr & 0xFF)); /* 发送存储地址低字节，补齐 EEPROM 内部地址，将无应答并入此前结果。 */

    /* 重复起始 + 读 */
    SoftI2C_Start(); /* 在 SCL 为高期间拉低 SDA，形成起始或重复起始条件。 */
    ack |= SoftI2C_SendByte((CAT24C256_ADDR << 1) | 0x01); /* 发送带读位的设备地址，切换到读数据阶段，将无应答并入此前结果。 */

    if (ack) /* 应答结果表明有 NACK 时进入错误分支，结束当前 I2C 事务。 */
    {
        g_last_read_nack = 1; /* 将最近一次 EEPROM 读取的无应答标志 更新为 「1」。 */
        SoftI2C_Stop(); /* 在 SCL 为高期间释放 SDA，结束本次总线事务。 */
        return;                                              /* NACK，提前终止 */
    }

    for (i = 0; i < len; i++) /* 从 「i = 0」 开始，在 「i < len」 成立时逐项处理，每轮执行 「i++」。 */
    {
        if (i == (len - 1)) /* 当 「i == (len - 1)」 成立时处理下方分支。 */
            buf[i] = SoftI2C_ReadByte(1);                    /* 最后一个字节: NACK */
        else /* 前面的条件不满足时执行此分支。 */
            buf[i] = SoftI2C_ReadByte(0);                    /* 继续读: ACK */
    }

    SoftI2C_Stop(); /* 在 SCL 为高期间释放 SDA，结束本次总线事务。 */
}

/*==========================================================================
 * EEPROM_WriteByte - 异步写入单字节
 *==========================================================================*/
EEPROM_Result_t EEPROM_WriteByte(uint16_t addr, uint8_t data) /* EEPROM 写入 字节。 */
{
    if (!EEPROM_IsIdle()) return EEPROM_BUSY; /* 仅在 EEPROM 写状态空闲时接受请求，防止覆盖上一笔异步写入的数据。 */
    if (addr > CAT24C256_ADDR_MASK) return EEPROM_ERROR; /* 拒绝超出 CAT24C256 物理地址范围的写入请求。 */

    g_single_byte_buf      = data;            /* 复制到静态缓冲区，避免栈指针悬空 */
    g_write_ctx.total_addr = addr; /* 将整笔写入的起始地址 更新为 「addr」。 */
    g_write_ctx.total_buf  = &g_single_byte_buf; /* 将整笔写入的源缓冲区指针 更新为 「&g_single_byte_buf」。 */
    g_write_ctx.total_len  = 1; /* 将整笔写入的字节数 更新为 「1」。 */
    g_write_ctx.done_len   = 0; /* 将已完成写入的字节数清零，使本轮从初始计数开始。 */

    _prepare_next_page(); /* 按页内剩余容量和总剩余量确定本次写入范围。 */
    _start_page_write(); /* 发送当前 EEPROM 页的地址和数据，并记录发送或应答异常状态。 */

    return EEPROM_OK; /* 将 「EEPROM_OK」 返回给调用者，供上层使用。 */
}

/*==========================================================================
 * EEPROM_WritePage - 异步写入一页 (len ≤ 64，不跨页)
 *
 * 首段在调用时同步发送。跨页时保留缓冲区和内容，供后续任务发送。
 *==========================================================================*/
EEPROM_Result_t EEPROM_WritePage(uint16_t addr, const uint8_t *buf, uint16_t len) /* EEPROM 写入 Page。 */
{
    if (!EEPROM_IsIdle()) return EEPROM_BUSY; /* 仅在 EEPROM 写状态空闲时接受请求，防止覆盖上一笔异步写入的数据。 */
    if (addr > CAT24C256_ADDR_MASK) return EEPROM_ERROR; /* 拒绝超出 CAT24C256 物理地址范围的写入请求。 */
    if (len == 0 || len > CAT24C256_PAGE_SIZE) return EEPROM_ERROR; /* 在开始 I2C 事务前拒绝不符合接口要求的长度。 */

    /* 检查是否跨页 */
    uint16_t page_start  = addr & ~(CAT24C256_PAGE_SIZE - 1); /* 定义本页的首地址，将本页的首地址 更新为 「addr & ~(CAT24C256_PAGE_SIZE - 1)」。 */
    uint16_t page_end    = page_start + CAT24C256_PAGE_SIZE - 1; /* 定义本页的末地址，将本页的末地址 更新为 「page_start + CAT24C256_PAGE_SIZE - 1」。 */
    if ((addr + len - 1) > page_end) return EEPROM_ERROR;  /* 不允许跨页 */

    g_write_ctx.total_addr = addr; /* 将整笔写入的起始地址 更新为 「addr」。 */
    g_write_ctx.total_buf  = buf; /* 将整笔写入的源缓冲区指针 更新为 「buf」。 */
    g_write_ctx.total_len  = len; /* 将整笔写入的字节数 更新为 「len」。 */
    g_write_ctx.done_len   = 0; /* 将已完成写入的字节数清零，使本轮从初始计数开始。 */

    _prepare_next_page(); /* 按页内剩余容量和总剩余量确定本次写入范围。 */
    _start_page_write(); /* 发送当前 EEPROM 页的地址和数据，并记录发送或应答异常状态。 */

    return EEPROM_OK; /* 将 「EEPROM_OK」 返回给调用者，供上层使用。 */
}

/*==========================================================================
 * EEPROM_WriteBuffer - 异步写入任意长度 (自动拆页)
 *
 * 内部自动按 64 字节页边界拆分，每页写完等待 5ms 再写下一页。
 *
 * 首段在调用时同步发送。跨页时保留缓冲区和内容，供后续任务发送。
 *==========================================================================*/
EEPROM_Result_t EEPROM_WriteBuffer(uint16_t addr, const uint8_t *buf, uint16_t len) /* EEPROM 写入 缓冲区。 */
{
    if (!EEPROM_IsIdle()) return EEPROM_BUSY; /* 仅在 EEPROM 写状态空闲时接受请求，防止覆盖上一笔异步写入的数据。 */
    if (addr > CAT24C256_ADDR_MASK) return EEPROM_ERROR; /* 拒绝超出 CAT24C256 物理地址范围的写入请求。 */
    if (len == 0) return EEPROM_ERROR; /* 在开始 I2C 事务前拒绝不符合接口要求的长度。 */
    if ((uint32_t)addr + len > CAT24C256_CAPACITY) return EEPROM_ERROR; /* 当 「(uint32_t)addr + len > CAT24C256_CAPACITY」 成立时，将 「EEPROM_ERROR」 返回给调用者，供上层使用。 */

    g_write_ctx.total_addr = addr; /* 将整笔写入的起始地址 更新为 「addr」。 */
    g_write_ctx.total_buf  = buf; /* 将整笔写入的源缓冲区指针 更新为 「buf」。 */
    g_write_ctx.total_len  = len; /* 将整笔写入的字节数 更新为 「len」。 */
    g_write_ctx.done_len   = 0; /* 将已完成写入的字节数清零，使本轮从初始计数开始。 */

    _prepare_next_page(); /* 按页内剩余容量和总剩余量确定本次写入范围。 */
    _start_page_write(); /* 发送当前 EEPROM 页的地址和数据，并记录发送或应答异常状态。 */

    return EEPROM_OK; /* 将 「EEPROM_OK」 返回给调用者，供上层使用。 */
}

/*==========================================================================
 * EEPROM_Task - 状态机推进函数 (每 100ms 调用)
 *==========================================================================*/
void EEPROM_Task(void) /* 推进异步写入的等待、拆页和完成状态，避免忙等内部写入周期。 */
{
    switch (g_state) /* 按g 状态的取值选择对应的处理分支。 */
    {
    case EEPROM_STATE_IDLE: /* 进入 EEPROM_STATE_IDLE 对应的状态处理分支。 */
    case EEPROM_STATE_DONE: /* 进入 EEPROM_STATE_DONE 对应的状态处理分支。 */
    case EEPROM_STATE_NACK: /* 进入 EEPROM_STATE_NACK 对应的状态处理分支。 */
        /* 空闲、完成、错误状态: 等待上层处理 */
        break; /* 结束本分支或当前循环，避免继续执行后面的分支或迭代。 */

    case EEPROM_STATE_SEND_PAGE: /* 进入 EEPROM_STATE_SEND_PAGE 对应的状态处理分支。 */
        /* 本页已在 _start_page_write 中完成，直接进入等待 */
        g_state = EEPROM_STATE_WAIT_WRITE; /* 切换到 EEPROM_STATE_WAIT_WRITE，下一次状态分发按该状态继续。 */
        g_write_ctx.wait_start_ms = delay_millis();   /* 记录 5ms 等待起点 */
        break; /* 结束本分支或当前循环，避免继续执行后面的分支或迭代。 */

    case EEPROM_STATE_WAIT_WRITE: /* 进入 EEPROM_STATE_WAIT_WRITE 对应的状态处理分支。 */
        /* 非阻塞等待 5ms EEPROM 内部写入周期 */
        if (delay_expired(g_write_ctx.wait_start_ms, CAT24C256_WRITE_CYCLE_MS)) /* 确认 EEPROM 页写入至少等待了规定时间，才允许统计完成或发送下一页。 */
        {
            /* 5ms 到，更新完成计数 */
            g_write_ctx.done_len += g_write_ctx.page_len; /* 将已完成写入的字节数 累加 本页实际写入的字节数。 */

            /* 检查是否还有数据要写 */
            if (g_write_ctx.done_len < g_write_ctx.total_len) /* 检查是否还有数据要写。 */
            {
                _prepare_next_page();       /* 准备下一页 */
                _start_page_write();        /* 发下一页 */
                /* 下一次 EEPROM_Task 调用时进入 SEND_PAGE → WAIT_WRITE */
            }
            else /* 前面的条件不满足时执行此分支。 */
            {
                g_state = EEPROM_STATE_DONE;  /* 全部完成 */
            }
        }
        break; /* 结束本分支或当前循环，避免继续执行后面的分支或迭代。 */
    }
}

/*==========================================================================
 * EEPROM_GetState - 查询当前状态
 *==========================================================================*/
EEPROM_State_t EEPROM_GetState(void) /* EEPROM 读取 状态。 */
{
    return g_state; /* 将 「g_state」 返回给调用者，供上层使用。 */
}

/*==========================================================================
 * EEPROM_IsIdle - 仅在 IDLE 返回 1。DONE 或 NACK 必须先确认。
 *==========================================================================*/
uint8_t EEPROM_IsIdle(void) /* EEPROM Is 空闲。 */
{
    return (g_state == EEPROM_STATE_IDLE) ? 1 : 0; /* 将 「(g_state == EEPROM_STATE_IDLE) ? 1 : 0」 返回给调用者，供上层使用。 */
}

/*==========================================================================
 * EEPROM_ClearState - 清除 DONE/NACK 状态回到 IDLE
 *==========================================================================*/
void EEPROM_ClearState(void) /* EEPROM 清除 状态。 */
{
    if (g_state == EEPROM_STATE_DONE || g_state == EEPROM_STATE_NACK) /* 当 「g_state == EEPROM_STATE_DONE || g_state == EEPROM_STATE_NACK」 成立时处理下方分支。 */
    {
        g_state = EEPROM_STATE_IDLE; /* 切换到 EEPROM_STATE_IDLE，下一次状态分发按该状态继续。 */
    }
}

/*==========================================================================
 * _prepare_next_page - 计算下一页的地址和长度
 *
 * 根据已完成的 done_len，计算下一页的起始地址和长度。
 * 自动处理页边界对齐。
 *==========================================================================*/
static void _prepare_next_page(void) /* 按页内剩余容量和总剩余量确定本次写入范围。 */
{
    uint16_t current_addr;       /* 本页起始地址         */
    uint16_t page_remain;        /* 当前页剩余空间        */
    uint16_t remain_len;         /* 总共还剩多少要写     */

    current_addr = g_write_ctx.total_addr + g_write_ctx.done_len; /* 将本页要写的起始地址 更新为 「g_write_ctx.total_addr + g_write_ctx.done_len」。 */
    remain_len   = g_write_ctx.total_len  - g_write_ctx.done_len; /* 将整笔写入剩余的数据长度 更新为 「g_write_ctx.total_len - g_write_ctx.done_len」。 */

    /* 当前页剩余空间 */
    page_remain = CAT24C256_PAGE_SIZE - (current_addr % CAT24C256_PAGE_SIZE); /* 将当前地址到页末的剩余空间 更新为 「CAT24C256_PAGE_SIZE - (current_addr % CAT24C256_PAGE_SIZE)」。 */

    /* 本页写入长度 = min(页剩余空间, 总剩余长度) */
    g_write_ctx.page_len = (remain_len < page_remain) ? remain_len : page_remain; /* 将本页实际写入的字节数 更新为 「(remain_len < page_remain) ? remain_len : page_remain」。 */
    g_write_ctx.page_addr = current_addr; /* 将当前页的写入起始地址 更新为 本页要写的起始地址。 */
    g_write_ctx.page_buf  = g_write_ctx.total_buf + g_write_ctx.done_len; /* 将当前页在源缓冲区中的起点 更新为 「g_write_ctx.total_buf + g_write_ctx.done_len」。 */
}

/*==========================================================================
 * _start_page_write - 同步发送一页 I2C 事务，耗时取决于字节数
 *
 * I2C 事务: START → 设备地址(W) → 地址高8位 → 地址低8位
 *                → 数据[0] → 数据[1] → ... → STOP
 *
 * 进来前确保: g_write_ctx.page_addr / page_buf / page_len 已就绪
 * 发送完成后: 状态切换到 SEND_PAGE，等待 EEPROM_Task 推进
 *
 * NACK 处理: 如果设备无应答，进入 NACK 错误状态
 *==========================================================================*/
static void _start_page_write(void) /* 发送当前 EEPROM 页的地址和数据，并记录发送或应答异常状态。 */
{
    uint16_t i; /* 声明遍历计数器，用来逐个处理指定范围内的元素。 */
    uint8_t  ack; /* 声明从设备的应答结果，为后续计算或读取保存中间值。 */

    SoftI2C_Start(); /* 在 SCL 为高期间拉低 SDA，形成起始或重复起始条件。 */

    /* 设备地址 + 写 */
    ack = SoftI2C_SendByte((CAT24C256_ADDR << 1) | 0x00); /* 发送带写位的设备地址，选择 EEPROM 并开始传输，保存设备应答结果。 */
    if (ack) /* 应答结果表明有 NACK 时进入错误分支，结束当前 I2C 事务。 */
    {
        SoftI2C_Stop(); /* 在 SCL 为高期间释放 SDA，结束本次总线事务。 */
        g_state = EEPROM_STATE_NACK; /* 切换到 EEPROM_STATE_NACK，下一次状态分发按该状态继续。 */
        return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */
    }

    /* 存储器地址高 8 位 */
    ack = SoftI2C_SendByte((uint8_t)(g_write_ctx.page_addr >> 8)); /* 发送存储地址高字节，先指定地址的高位，保存设备应答结果。 */
    if (ack) /* 应答结果表明有 NACK 时进入错误分支，结束当前 I2C 事务。 */
    {
        SoftI2C_Stop(); /* 在 SCL 为高期间释放 SDA，结束本次总线事务。 */
        g_state = EEPROM_STATE_NACK; /* 切换到 EEPROM_STATE_NACK，下一次状态分发按该状态继续。 */
        return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */
    }

    /* 存储器地址低 8 位 */
    ack = SoftI2C_SendByte((uint8_t)(g_write_ctx.page_addr & 0xFF)); /* 发送存储地址低字节，补齐 EEPROM 内部地址，保存设备应答结果。 */
    if (ack) /* 应答结果表明有 NACK 时进入错误分支，结束当前 I2C 事务。 */
    {
        SoftI2C_Stop(); /* 在 SCL 为高期间释放 SDA，结束本次总线事务。 */
        g_state = EEPROM_STATE_NACK; /* 切换到 EEPROM_STATE_NACK，下一次状态分发按该状态继续。 */
        return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */
    }

    /* 发送数据字节 */
    for (i = 0; i < g_write_ctx.page_len; i++) /* 从 「i = 0」 开始，在 「i < g_write_ctx.page_len」 成立时逐项处理，每轮执行 「i++」。 */
    {
        ack = SoftI2C_SendByte(g_write_ctx.page_buf[i]); /* 发送本步 I2C 数据字节，并取得设备应答，保存设备应答结果。 */
        if (ack) /* 应答结果表明有 NACK 时进入错误分支，结束当前 I2C 事务。 */
        {
            SoftI2C_Stop(); /* 在 SCL 为高期间释放 SDA，结束本次总线事务。 */
            g_state = EEPROM_STATE_NACK; /* 切换到 EEPROM_STATE_NACK，下一次状态分发按该状态继续。 */
            return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */
        }
    }

    SoftI2C_Stop(); /* 在 SCL 为高期间释放 SDA，结束本次总线事务。 */
    g_state = EEPROM_STATE_SEND_PAGE; /* 切换到 EEPROM_STATE_SEND_PAGE，下一次状态分发按该状态继续。 */
}

/*==========================================================================
 * EEPROM_ReadHadNack - 查询上次读取是否遇到 NACK
 *
 * 返回: 0 = 读取正常,  1 = 上次读取遇到 NACK (设备未应答)
 *==========================================================================*/
uint8_t EEPROM_ReadHadNack(void) /* EEPROM 读取 Had Nack。 */
{
    return g_last_read_nack; /* 将 最近一次 EEPROM 读取的无应答标志 返回给调用者，供上层使用。 */
}
