#include "bw16_uart.h"
#include "stm32f10x.h"

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
#define RX_SIZE 512U
#define TX_SIZE (BW16_UART_MAX_WRITE + 1U)
/* PE 为校验错误，FE 为帧格式错误，NE 为噪声。ORE 只说明后续字节丢失，单独处理。 */
#define RX_BAD_BYTE (USART_SR_PE | USART_SR_FE | USART_SR_NE)

static volatile uint8_t rx_buffer[RX_SIZE];
static volatile uint16_t rx_head, rx_tail;
static volatile uint32_t rx_faults;
static volatile uint8_t tx_buffer[TX_SIZE];
static volatile uint16_t tx_head, tx_tail;

bool BW16_UART_Init(uint32_t baudrate)
{
    RCC_ClocksTypeDef clocks;
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef uart;
    NVIC_InitTypeDef irq;

    /* UART5 用 APB1 时钟。用实际 PCLK1 计算范围，避免把 CPU 主频当成串口时钟。
     * 16 倍过采样时，折算后的 BRR 分频值应在 16 到 65535 之间。
     * 最小波特率用向上取整，最大波特率用向下取整。先检查再动寄存器，
     * 这样非法参数不会清掉已经排队的收发数据。
     */
    RCC_GetClocksFreq(&clocks);
    if (baudrate < (clocks.PCLK1_Frequency + 65534U) / 65535U ||
        baudrate > clocks.PCLK1_Frequency / 16U) {
        return false;
    }

    /* 重新初始化时先关 UART5 中断，防止中断在寄存器或队列重置到一半时访问它们。
     * GPIOC/GPIOD 挂在 APB2，UART5 挂在 APB1，分别打开对应时钟。
     * 重置外设和下标即可丢弃旧数据，数组内容不必逐字节清零。
     */
    NVIC_DisableIRQ(UART5_IRQn);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART5, ENABLE);
    USART_DeInit(UART5);
    rx_head = rx_tail = 0U;
    tx_head = tx_tail = 0U;
    rx_faults = 0U;

    /* STM32 第 53 脚 PC12 由 UART5 驱动，配置为复用推挽，接 BW16 AT_RX。
     * 第 54 脚 PD2 接 BW16 AT_TX，配置为上拉输入，在对端尚未驱动时保持空闲高电平。
     * GPIO_Speed 指输出驱动速度，通信波特率由 UART 寄存器单独设置。
     */
    gpio.GPIO_Pin = GPIO_Pin_12;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOC, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_2;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOD, &gpio);

    /* 标准库默认启用收发，格式为 8 位数据、无校验、1 位停止位且无硬件流控。
     * 只覆盖波特率。先清 NVIC 的旧挂起标志，再开 RXNE，接收从启用外设时开始。
     * TXE 在空闲时会一直为 1，等有数据写入发送队列时再打开它的中断。
     */
    USART_StructInit(&uart);
    uart.USART_BaudRate = baudrate;
    USART_Init(UART5, &uart);
    NVIC_ClearPendingIRQ(UART5_IRQn);
    USART_ITConfig(UART5, USART_IT_RXNE, ENABLE);
    USART_Cmd(UART5, ENABLE);

    /* 工程先在 NVIC_Configuration() 设分组。抢占优先级 2 让现有优先级 0/1 的
     * 屏幕和日志 DMA 中断先运行。UART5 中断只处理寄存器和队列，尽快返回。
     */
    irq.NVIC_IRQChannel = UART5_IRQn;
    irq.NVIC_IRQChannelPreemptionPriority = 2U;
    irq.NVIC_IRQChannelSubPriority = 0U;
    irq.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&irq);
    return true;
}

bool BW16_UART_Write(const uint8_t *data, uint16_t length)
{
    /* 先将写位置保存在局部变量。中断只能从已公布的 tx_head 之前取数据。
     * 空闲量等于从 head 到 tail 的距离再减掉保留的那一格，取模处理下标跨数组末尾。
     * 计算空闲量后，中断只可能多清出空间，按这次计算出的容量检查是安全的。
     */
    uint16_t head = tx_head;
    uint16_t free_slots = (tx_tail + TX_SIZE - head - 1U) % TX_SIZE;
    uint16_t i;

    /* UE 表示 UART 已启用。空间不足时整段拒绝，让上层可以重试同一段数据，
     * 例如 AT 口的一整条指令。长度为 0 时无需读取源指针。
     */
    if (!(UART5->CR1 & USART_CR1_UE) || length > free_slots ||
        (data == 0 && length != 0U)) {
        return false;
    }
    if (length != 0U) {
        for (i = 0U; i < length; ++i) {
            tx_buffer[head] = data[i];
            head = (head + 1U) % TX_SIZE;
        }
        /* 全部拷贝完再更新共享下标。这样中断只能看到完整入队的数据，
         * 即使发送队列原先为空，也会在这里开启 TXE 中断开始发送。
         */
        tx_head = head;
        USART_ITConfig(UART5, USART_IT_TXE, ENABLE);
    }
    return true;
}

uint16_t BW16_UART_Read(uint8_t *data, uint16_t capacity)
{
    /* 只取已收到的字节，最多写入 capacity 个。每取一个就释放对应槽位，
     * 中断随后可以复用。队列空就立即返回，主循环可以继续处理屏幕等任务。
     */
    uint16_t count = 0U;
    if (data != 0) {
        while (count < capacity && rx_tail != rx_head) {
            data[count++] = rx_buffer[rx_tail];
            rx_tail = (rx_tail + 1U) % RX_SIZE;
        }
    }
    return count;
}

uint32_t BW16_UART_RxFaultCount(void)
{
    /* 返回累计事件数。上层可比较组帧前后的数值，发现变化时丢弃残缺帧并重新找帧头。 */
    return rx_faults;
}

void UART5_IRQHandler(void)
{
    uint16_t status = UART5->SR;

    /* 先处理接收，因为接收数据寄存器里未及时取出的字节会造成硬件溢出。 */
    if (status & (USART_SR_RXNE | USART_SR_ORE | RX_BAD_BYTE)) {
        /* STM32F1 要按先 SR 后 DR 的顺序读取，才能清掉接收和错误标志。
         * ORE 表示新字节到达时旧字节还在 DR。旧字节仍可在 RXNE 置位且本身无错误时入队。
         */
        uint8_t byte = (uint8_t)UART5->DR;
        uint16_t next = (rx_head + 1U) % RX_SIZE;
        /* 若软件队列已满，保留早先入队的数据，丢弃本次收到的字节。
         * 同次中断出现多种接收问题只累计一次，因此这是故障事件数。
         */
        if ((status & (USART_SR_ORE | RX_BAD_BYTE)) || next == rx_tail) {
            ++rx_faults;
        }
        if ((status & USART_SR_RXNE) && !(status & RX_BAD_BYTE) && next != rx_tail) {
            rx_buffer[rx_head] = byte;
            rx_head = next;
        }
    }

    /* 同次中断可同时收和发。TXE 表示发送数据寄存器可以装下一个字节，
     * 此时上一个字节可能仍在移位输出。队列空时关闭 TXE 中断，避免空闲时反复进中断。
     */
    if ((status & USART_SR_TXE) && (UART5->CR1 & USART_CR1_TXEIE)) {
        if (tx_tail == tx_head) {
            USART_ITConfig(UART5, USART_IT_TXE, DISABLE);
        } else {
            UART5->DR = tx_buffer[tx_tail];
            tx_tail = (tx_tail + 1U) % TX_SIZE;
        }
    }
}
