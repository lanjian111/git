#include "bw16_uart.h" /* 使 UART5 的 BW16 收发实现和公开接口保持一致。 */
#include "stm32f10x.h" /* 使用 STM32F1 的寄存器、外设类型和基于工程配置启用的驱动接口。 */

/*
 * UART5 连接板上的 BW16。此层负责搬运字节，AT 应答、超时和上层协议由调用方处理。
 *
 * 两个环形队列均约定：head 指向下一个可写位置，tail 指向下一个可读位置。
 * head == tail 表示空，head 再前进一格会碰到 tail 时表示满，因此始终留一个空位。
 * 数组长度分别是 512 和 256，对应有效容量 511 和 255 字节。
 *
 * 主循环和中断各自只更新自己负责的下标，让收发时不用长时间关闭中断。
 * RX 由中断写、主循环读，TX 由主循环写、中断读。共享数据用 volatile，
 * 确保主循环和 ISR 各自实际读写共享槽位和下标。队列依赖单生产者和单消费者约定。
 */
#define RX_SIZE 512U /* 为接收环形队列留 512 个槽，有效容量为 511 字节。 */
#define TX_SIZE (BW16_UART_MAX_WRITE + 1U) /* 在单次最大写入容量之外多留一格，以区分队列满和空。 */
/* PE 为校验错误，FE 为帧格式错误，NE 为噪声。ORE 只说明后续字节丢失，单独处理。 */
#define RX_BAD_BYTE (USART_SR_PE | USART_SR_FE | USART_SR_NE) /* 合并校验、帧格式和噪声位，供中断判断本字节是否可用。 */

static volatile uint8_t rx_buffer[RX_SIZE]; /* 分配 「rx_buffer[RX_SIZE]」 数组，作为当前操作的数据缓冲区。 */
static volatile uint16_t rx_head, rx_tail; /* 声明 「rx_head, rx_tail」，用于本函数的中间结果和循环控制。 */
static volatile uint32_t rx_faults; /* 声明UART5 累计接收故障事件数，为后续计算或读取保存中间值。 */
static volatile uint8_t tx_buffer[TX_SIZE]; /* 分配 「tx_buffer[TX_SIZE]」 数组，作为当前操作的数据缓冲区。 */
static volatile uint16_t tx_head, tx_tail; /* 声明 「tx_head, tx_tail」，用于本函数的中间结果和循环控制。 */

bool BW16_UART_Init(uint32_t baudrate) /* 按指定波特率初始化与 BW16 相连的 UART5，并开启中断接收。 */
{
    RCC_ClocksTypeDef clocks; /* 接收时钟树频率，UART5 波特率检查会使用 PCLK1。 */
    GPIO_InitTypeDef gpio; /* 准备 GPIO 配置结构体，依次给 UART5 的发送和接收引脚赋值。 */
    USART_InitTypeDef uart; /* 保存 UART5 数据格式及波特率，供标准库写入寄存器。 */
    NVIC_InitTypeDef irq; /* 保存 UART5 的中断通道和优先级配置。 */

    /* UART5 用 APB1 时钟。用实际 PCLK1 计算范围，避免把 CPU 主频当成串口时钟。
     * 16 倍过采样时，折算后的 BRR 分频值应在 16 到 65535 之间。
     * 最小波特率用向上取整，最大波特率用向下取整。先检查再动寄存器，
     * 这样非法参数不会清掉已经排队的收发数据。
     */
    RCC_GetClocksFreq(&clocks); /* 读取当前实际时钟树，后续用 APB1 的频率计算 UART5 波特率边界。 */
    if (baudrate < (clocks.PCLK1_Frequency + 65534U) / 65535U || /* 用向上取整检查最低波特率，避免 BRR 分频值超过 16 位。 */
        baudrate > clocks.PCLK1_Frequency / 16U) { /* 同时限制最高波特率，使 16 倍过采样下的分频值至少为 16。 */
        return false; /* 向调用者报告本次操作被拒绝或检查未通过。 */
    }

    /* 重新初始化时先关 UART5 中断，防止中断在寄存器或队列重置到一半时访问它们。
     * GPIOC/GPIOD 挂在 APB2，UART5 挂在 APB1，分别打开对应时钟。
     * 重置外设和下标即可丢弃旧数据，数组内容不必逐字节清零。
     */
    NVIC_DisableIRQ(UART5_IRQn); /* 先关闭 UART5 中断，防止重配过程中中断读取尚未初始化的寄存器和队列。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD, ENABLE); /* 开启 「RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD」 的外设时钟，控制相关外设时钟供给。 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART5, ENABLE); /* 开启 「RCC_APB1Periph_UART5」 的外设时钟，控制相关外设时钟供给。 */
    USART_DeInit(UART5); /* 将 UART5 复位到默认寄存器状态，消除上次配置遗留。 */
    rx_head = rx_tail = 0U; /* 让接收队列读写下标相等，丢弃之前尚未读取的数据。 */
    tx_head = tx_tail = 0U; /* 让发送队列读写下标相等，丢弃之前尚未发送的数据。 */
    rx_faults = 0U; /* 开始新一轮故障统计，清除上次运行的计数。 */

    /* STM32 第 53 脚 PC12 由 UART5 驱动，配置为复用推挽，接 BW16 AT_RX。
     * 第 54 脚 PD2 接 BW16 AT_TX，配置为上拉输入，在对端尚未驱动时保持空闲高电平。
     * GPIO_Speed 指输出驱动速度，通信波特率由 UART 寄存器单独设置。
     */
    gpio.GPIO_Pin = GPIO_Pin_12; /* 选定 PC12，后续把它配置为 UART5 的发送脚。 */
    gpio.GPIO_Speed = GPIO_Speed_50MHz; /* 设置输出驱动速度档位，串口位速率另外配置。 */
    gpio.GPIO_Mode = GPIO_Mode_AF_PP; /* 选择复用推挽，让 UART5 外设主动驱动发送脚。 */
    GPIO_Init(GPIOC, &gpio); /* 把上述复用输出配置应用到 GPIOC 的 PC12。 */
    gpio.GPIO_Pin = GPIO_Pin_2; /* 选定 PD2，后续把它配置为 UART5 的接收脚。 */
    gpio.GPIO_Mode = GPIO_Mode_IPU; /* 选择上拉输入，让 BW16 未驱动时接收线也保持 UART 空闲高电平。 */
    GPIO_Init(GPIOD, &gpio); /* 把上拉输入配置应用到 GPIOD 的 PD2。 */

    /* 标准库默认启用收发，格式为 8 位数据、无校验、1 位停止位且无硬件流控。
     * 只覆盖波特率。先清 NVIC 的旧挂起标志，再开 RXNE，接收从启用外设时开始。
     * TXE 在空闲时会一直为 1，等有数据写入发送队列时再打开它的中断。
     */
    USART_StructInit(&uart); /* 先使用标准库默认的 8N1、全双工收发和无硬件流控配置。 */
    uart.USART_BaudRate = baudrate; /* 以已校验的调用者波特率覆盖默认值。 */
    USART_Init(UART5, &uart); /* 按上述数据格式及速率配置 UART5 寄存器。 */
    NVIC_ClearPendingIRQ(UART5_IRQn); /* 清除重配前遗留的中断挂起请求。 */
    USART_ITConfig(UART5, USART_IT_RXNE, ENABLE); /* 允许收到新字节时触发中断，及时从数据寄存器取走字节。 */
    USART_Cmd(UART5, ENABLE); /* 开启已配置的 UART5 外设，使接收开始工作。 */

    /* 工程先在 NVIC_Configuration() 设分组。抢占优先级 2 让现有优先级 0/1 的
     * 屏幕和日志 DMA 中断先运行。UART5 中断只处理寄存器和队列，尽快返回。
     */
    irq.NVIC_IRQChannel = UART5_IRQn; /* 将本次配置的中断通道 更新为 「UART5_IRQn」。 */
    irq.NVIC_IRQChannelPreemptionPriority = 2U; /* 将 UART5 抢占优先级设为 2，让现有优先级 0 和 1 的 DMA 中断可先运行。 */
    irq.NVIC_IRQChannelSubPriority = 0U; /* 设置同抢占等级下的子优先级。 */
    irq.NVIC_IRQChannelCmd = ENABLE; /* 将中断通道使能配置 更新为 启用（ENABLE）。 */
    NVIC_Init(&irq); /* 把通道号和优先级应用到中断控制器，允许 UART5 调用中断服务。 */
    return true; /* 报告本次参数检查和操作成功，由调用者继续处理。 */
}

bool BW16_UART_Write(const uint8_t *data, uint16_t length) /* 在发送队列中整段提交数据，供 UART5 发送中断逐字节取走。 */
{
    /* 先将写位置保存在局部变量。中断只能从已公布的 tx_head 之前取数据。
     * 空闲量等于从 head 到 tail 的距离再减掉保留的那一格，取模处理下标跨数组末尾。
     * 计算空闲量后，中断只可能多清出空间，按这次计算出的容量检查是安全的。
     */
    uint16_t head = tx_head; /* 暂存发送写下标，等整段拷贝完再一次性公布。 */
    uint16_t free_slots = (tx_tail + TX_SIZE - head - 1U) % TX_SIZE; /* 用取模计算可写空间，并扣掉用于区分满和空的保留槽。 */
    uint16_t i; /* 声明遍历计数器，用来逐个处理指定范围内的元素。 */

    /* UE 表示 UART 已启用。空间不足时整段拒绝，让上层可以重试同一段数据，
     * 例如 AT 口的一整条指令。长度为 0 时无需读取源指针。
     */
    if (!(UART5->CR1 & USART_CR1_UE) || length > free_slots || /* UART5 未开启或队列无法容纳整段数据时拒绝写入，由上层决定何时重试。 */
        (data == 0 && length != 0U)) { /* 需写入字节却没有源地址时拒绝请求，避免解引用空指针。 */
        return false; /* 向调用者报告本次操作被拒绝或检查未通过。 */
    }
    if (length != 0U) { /* 有字节需要发送时才复制并打开 TXE 中断。 */
        for (i = 0U; i < length; ++i) { /* 从 「i = 0U」 开始，在 「i < length」 成立时逐项处理，每轮执行 「++i」。 */
            tx_buffer[head] = data[i]; /* 把本字节写入尚未公布的槽位，让发送中断只能读取已经提交的数据。 */
            head = (head + 1U) % TX_SIZE; /* 移动局部写下标，走到数组末尾后回到起点。 */
        }
        /* 全部拷贝完再更新共享下标。这样中断只能看到完整入队的数据，
         * 即使发送队列原先为空，也会在这里开启 TXE 中断开始发送。
         */
        tx_head = head; /* 一次性公布更新后的写下标，使发送中断可以看到刚入队的整段字节。 */
        USART_ITConfig(UART5, USART_IT_TXE, ENABLE); /* 使能发送寄存器空中断，在寄存器可写时从队列取数据。 */
    }
    return true; /* 报告本次参数检查和操作成功，由调用者继续处理。 */
}

uint16_t BW16_UART_Read(uint8_t *data, uint16_t capacity) /* 复制 UART5 已收到的字节，最多写入调用方提供的容量。 */
{
    /* 只取已收到的字节，最多写入 capacity 个。每取一个就释放对应槽位，
     * 中断随后可以复用。队列空就立即返回，主循环可以继续处理屏幕等任务。
     */
    uint16_t count = 0U; /* 从零统计复制到调用方缓冲区的字节数。 */
    if (data != 0) { /* 只有调用方提供有效输出地址时才从接收队列复制。 */
        while (count < capacity && rx_tail != rx_head) { /* 缓冲区还有空间且队列非空时继续读，保证写入量不超过容量。 */
            data[count++] = rx_buffer[rx_tail]; /* 将当前未读槽复制到输出缓冲区，并把输出计数推进一格。 */
            rx_tail = (rx_tail + 1U) % RX_SIZE; /* 释放已经读出的槽位，读下标按环形容量回卷。 */
        }
    }
    return count; /* 返回实际复制的字节数，队列空或没有输出地址时返回零。 */
}

uint32_t BW16_UART_RxFaultCount(void) /* 读取累计接收故障次数，供上层检测接收流是否中断。 */
{
    /* 返回累计事件数。上层可比较组帧前后的数值，发现变化时丢弃残缺帧并重新找帧头。 */
    return rx_faults; /* 返回累计接收故障事件数，供上层通过计数变化识别可能缺字节的帧。 */
}

void UART5_IRQHandler(void) /* 响应 UART5 中断，搬运收发字节并处理接收故障。 */
{
    uint16_t status = UART5->SR; /* 先保存状态寄存器快照，为随后读取 DR 并清接收错误做好顺序要求。 */

    /* 先处理接收，因为接收数据寄存器里未及时取出的字节会造成硬件溢出。 */
    if (status & (USART_SR_RXNE | USART_SR_ORE | RX_BAD_BYTE)) { /* 有接收数据或错误时先处理接收，及时读取 DR 以释放接收数据寄存器。 */
        /* STM32F1 要按先 SR 后 DR 的顺序读取，才能清掉接收和错误标志。
         * ORE 表示新字节到达时旧字节还在 DR。旧字节仍可在 RXNE 置位且本身无错误时入队。
         */
        uint8_t byte = (uint8_t)UART5->DR; /* 按先读 SR 后读 DR 的顺序取出数据字节并清除相应接收或错误标志。 */
        uint16_t next = (rx_head + 1U) % RX_SIZE; /* 预计算下一写入位置，以便用它是否追上读下标判断队列是否满。 */
        /* 若软件队列已满，保留早先入队的数据，丢弃本次收到的字节。
         * 同次中断出现多种接收问题只累计一次，因此这是故障事件数。
         */
        if ((status & (USART_SR_ORE | RX_BAD_BYTE)) || next == rx_tail) { /* 硬件丢字节、当前字节有错误或软件队列满时计入一次故障事件。 */
            ++rx_faults; /* 增加一次接收故障事件计数，同次中断中多个接收原因合并计数。 */
        }
        if ((status & USART_SR_RXNE) && !(status & RX_BAD_BYTE) && next != rx_tail) { /* 只有确实收到的字节无错误且队列有空位时才入队。 */
            rx_buffer[rx_head] = byte; /* 先把通过检查的字节放到接收缓冲区当前写槽。 */
            rx_head = next; /* 数据写好后更新写下标，让主循环可以读到刚存入的字节。 */
        }
    }

    /* 同次中断可同时收和发。TXE 表示发送数据寄存器可以装下一个字节，
     * 此时上一个字节可能仍在移位输出。队列空时关闭 TXE 中断，避免空闲时反复进中断。
     */
    if ((status & USART_SR_TXE) && (UART5->CR1 & USART_CR1_TXEIE)) { /* 发送寄存器可写且 TXE 中断已开启时才处理发送。 */
        if (tx_tail == tx_head) { /* 读写下标相等表示发送队列为空，可以停止发送中断。 */
            USART_ITConfig(UART5, USART_IT_TXE, DISABLE); /* 队列为空时关 TXE 中断，避免空闲寄存器不断触发中断。 */
        } else { /* 前面的条件不满足时执行此分支。 */
            UART5->DR = tx_buffer[tx_tail]; /* 把下一个待发字节写入 UART5 数据寄存器，由硬件串行发出。 */
            tx_tail = (tx_tail + 1U) % TX_SIZE; /* 释放已交给 UART5 的发送槽，读下标到数组末尾后回卷。 */
        }
    }
}
