#include "stm32f10x.h"  // STM32 标准外设头
#include "misc.h" // 中断和工具头
#include "USARTDMA.h" // 本模块头
#include <stdio.h> // printf 支持
#include <stdarg.h> // 可变参数
#include <string.h> // 字符串函数

/* =============== 全局变量定义 =============== */
USART_DMA_HandleTypeDef husart1; // USART1 句柄
USART_DMA_HandleTypeDef husart3; // USART3 句柄

/* =============== 静态函数声明 =============== */
static void USART_DMA_GPIO_Init(void); // 初始化 USART1 GPIO
static void USART_DMA_USART_Init(uint32_t baudrate); // 初始化 USART1 外设
static void USART_DMA_DMA_Init(void); // 初始化 USART1 DMA
static void USART_DMA_NVIC_Init(void); // 初始化 USART1 NVIC

static void USART3_DMA_GPIO_Init(void); // 初始化 USART3 GPIO
static void USART3_DMA_USART_Init(uint32_t baudrate); // 初始化 USART3 外设
static void USART3_DMA_DMA_Init(void); // 初始化 USART3 DMA
static void USART3_DMA_NVIC_Init(void); // 初始化 USART3 NVIC

/* =============== 初始化函数 =============== */

/**
  * @brief  初始化USART1 DMA
  * @param  baudrate: 波特率
  * @retval None
  */
void USART_DMA_Init(uint32_t baudrate)
{
    // 初始化句柄
    memset(&husart1, 0, sizeof(husart1)); // 清零句柄
    husart1.tx_busy = false; // 发送空闲
    husart1.rx_idle = true; // 接收空闲
    husart1.rx_write_pos = 0; // 写指针归零
    husart1.rx_read_pos = 0; // 读指针归零
    husart1.rx_available = 0; // 可用字节清零
    husart1.rx_callback = NULL; // 回调置空
    husart1.tx_complete_callback = NULL; // 回调置空
    
    // 使能时钟
    RCC_APB2PeriphClockCmd(USART_DMA_GPIO_CLK | USART_DMA_USART_CLK, ENABLE); // GPIO/USART 时钟
    RCC_AHBPeriphClockCmd(USART_DMA_DMA_CLK, ENABLE); // DMA 时钟
    
    // 初始化各外设
    USART_DMA_GPIO_Init(); // 初始化 GPIO
    USART_DMA_USART_Init(baudrate); // 初始化 USART
    USART_DMA_DMA_Init(); // 初始化 DMA
    USART_DMA_NVIC_Init(); // 初始化 NVIC

    // 同时初始化USART3(PB10/PB11) DMA，用于HMI
    USART3_DMA_Init(baudrate); // 初始化 USART3 DMA
}

/**
  * @brief  反初始化USART1 DMA
  * @retval None
  */
void USART_DMA_DeInit(void)
{
    // 禁用DMA
    DMA_Cmd(USART_DMA_TX_CHANNEL, DISABLE); // 关闭 TX DMA
    DMA_Cmd(USART_DMA_RX_CHANNEL, DISABLE); // 关闭 RX DMA
    
    // 禁用USART
    USART_Cmd(USART_DMA_USARTx, DISABLE); // 关闭 USART
    
    // 禁用中断
    NVIC_DisableIRQ(USART_DMA_TX_IRQn); // 关闭 TX 中断
    NVIC_DisableIRQ(USART_DMA_RX_IRQn); // 关闭 RX 中断
    
    // 复位句柄
    memset(&husart1, 0, sizeof(husart1)); // 清零句柄
}

/**
    * @brief  初始化USART3 DMA
    * @param  baudrate: 波特率
    * @retval None
    */
void USART3_DMA_Init(uint32_t baudrate)
{
        // 初始化句柄
    memset(&husart3, 0, sizeof(husart3)); // 清零句柄
    husart3.tx_busy = false; // 发送空闲
    husart3.rx_idle = true; // 接收空闲
    husart3.rx_write_pos = 0; // 写指针归零
    husart3.rx_read_pos = 0; // 读指针归零
    husart3.rx_available = 0; // 可用字节清零
    husart3.rx_callback = NULL; // 回调置空
    husart3.tx_complete_callback = NULL; // 回调置空

        // 使能时钟
        RCC_APB2PeriphClockCmd(USART3_DMA_GPIO_CLK, ENABLE); // GPIO 时钟
        RCC_APB1PeriphClockCmd(USART3_DMA_USART_CLK, ENABLE); // USART3 时钟
        RCC_AHBPeriphClockCmd(USART_DMA_DMA_CLK, ENABLE); // DMA 时钟

        // 初始化各外设
        USART3_DMA_GPIO_Init(); // 初始化 GPIO
        USART3_DMA_USART_Init(baudrate); // 初始化 USART3
        USART3_DMA_DMA_Init(); // 初始化 DMA
        USART3_DMA_NVIC_Init(); // 初始化 NVIC
    }

/**
    * @brief  反初始化USART3 DMA
    * @retval None
    */
void USART3_DMA_DeInit(void)
{
        // 禁用DMA
    DMA_Cmd(USART3_DMA_TX_CHANNEL, DISABLE); // 关闭 TX DMA
    DMA_Cmd(USART3_DMA_RX_CHANNEL, DISABLE); // 关闭 RX DMA

        // 禁用USART
        USART_Cmd(USART3_DMA_USARTx, DISABLE); // 关闭 USART3

        // 禁用中断
        NVIC_DisableIRQ(USART3_DMA_TX_IRQn); // 关闭 TX 中断
        NVIC_DisableIRQ(USART3_DMA_RX_IRQn); // 关闭 RX 中断

        // 复位句柄
        memset(&husart3, 0, sizeof(husart3)); // 清零句柄
    }

/* =============== 静态初始化函数 =============== */

/**
  * @brief  初始化GPIO
  * @retval None
  */
static void USART_DMA_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure; // GPIO 配置结构体
    
    // 配置TX引脚（PA9）
    GPIO_InitStructure.GPIO_Pin = USART_DMA_TX_PIN; // TX 引脚
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;      // 复用推挽输出
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; // 速度配置
    GPIO_Init(USART_DMA_GPIOx, &GPIO_InitStructure); // 初始化 TX 引脚
    
    // 配置RX引脚（PA10）
    GPIO_InitStructure.GPIO_Pin = USART_DMA_RX_PIN; // RX 引脚
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; // 浮空输入
    GPIO_Init(USART_DMA_GPIOx, &GPIO_InitStructure); // 初始化 RX 引脚
}

/**
    * @brief  初始化USART3 GPIO
    * @retval None
    */
static void USART3_DMA_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure; // GPIO 配置结构体

        // 配置TX引脚（PB10）
        GPIO_InitStructure.GPIO_Pin = USART3_DMA_TX_PIN; // TX 引脚
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; // 复用推挽
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; // 速度配置
        GPIO_Init(USART3_DMA_GPIOx, &GPIO_InitStructure); // 初始化 TX 引脚

        // 配置RX引脚（PB11）
        GPIO_InitStructure.GPIO_Pin = USART3_DMA_RX_PIN; // RX 引脚
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; // 浮空输入
        GPIO_Init(USART3_DMA_GPIOx, &GPIO_InitStructure); // 初始化 RX 引脚
    }

/**
  * @brief  初始化USART
  * @param  baudrate: 波特率
  * @retval None
  */
static void USART_DMA_USART_Init(uint32_t baudrate)
{
    USART_InitTypeDef USART_InitStructure; // USART 配置结构体
    
    USART_InitStructure.USART_BaudRate = baudrate; // 波特率
    USART_InitStructure.USART_WordLength = USART_WordLength_8b; // 8 位数据
    USART_InitStructure.USART_StopBits = USART_StopBits_1; // 1 位停止位
    USART_InitStructure.USART_Parity = USART_Parity_No; // 无校验
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; // 无硬件流控
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; // 收发使能
    
    USART_Init(USART_DMA_USARTx, &USART_InitStructure); // 初始化 USART
    
    // 使能USART DMA请求
    USART_DMACmd(USART_DMA_USARTx, USART_DMAReq_Tx, ENABLE); // 使能 TX DMA
    USART_DMACmd(USART_DMA_USARTx, USART_DMAReq_Rx, ENABLE); // 使能 RX DMA
    
    // 使能USART
    USART_Cmd(USART_DMA_USARTx, ENABLE); // 使能 USART
}

/**
    * @brief  初始化USART3
    * @param  baudrate: 波特率
    * @retval None
    */
static void USART3_DMA_USART_Init(uint32_t baudrate)
{
    USART_InitTypeDef USART_InitStructure; // USART3 配置结构体

        USART_InitStructure.USART_BaudRate = baudrate; // 波特率
        USART_InitStructure.USART_WordLength = USART_WordLength_8b; // 8 位数据
        USART_InitStructure.USART_StopBits = USART_StopBits_1; // 1 位停止位
        USART_InitStructure.USART_Parity = USART_Parity_No; // 无校验
        USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; // 无硬件流控
        USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; // 收发使能

        USART_Init(USART3_DMA_USARTx, &USART_InitStructure); // 初始化 USART3

        // 使能USART DMA请求
        USART_DMACmd(USART3_DMA_USARTx, USART_DMAReq_Tx, ENABLE); // 使能 TX DMA
        USART_DMACmd(USART3_DMA_USARTx, USART_DMAReq_Rx, ENABLE); // 使能 RX DMA

        // 使能USART
        USART_Cmd(USART3_DMA_USARTx, ENABLE); // 使能 USART3
    }

/**
  * @brief  初始化DMA
  * @retval None
  */
static void USART_DMA_DMA_Init(void)
{
    DMA_InitTypeDef DMA_InitStructure; // DMA 配置结构体
    
    /* =============== DMA发送配置 =============== */
    DMA_DeInit(USART_DMA_TX_CHANNEL); // 复位 TX DMA
    
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART_DMA_USARTx->DR; // 外设地址
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)husart1.tx_buffer; // 内存地址
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;           // 内存到外设
    DMA_InitStructure.DMA_BufferSize = 0;                        // 初始为0
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable; // 外设不自增
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable; // 内存自增
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; // 外设字节宽度
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte; // 内存字节宽度
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;                // 正常模式
    DMA_InitStructure.DMA_Priority = DMA_Priority_High; // 高优先级
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable; // 关闭 M2M
    
    DMA_Init(USART_DMA_TX_CHANNEL, &DMA_InitStructure); // 初始化 TX DMA
    
    // 使能发送完成中断
    DMA_ITConfig(USART_DMA_TX_CHANNEL, DMA_IT_TC, ENABLE); // 使能发送完成中断
    
    /* =============== DMA接收配置 =============== */
    DMA_DeInit(USART_DMA_RX_CHANNEL); // 复位 RX DMA
    
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART_DMA_USARTx->DR; // 外设地址
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)husart1.rx_buffer; // 内存地址
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;           // 外设到内存
    DMA_InitStructure.DMA_BufferSize = USART_DMA_RX_BUFFER_SIZE; // 循环缓冲区大小
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable; // 外设不自增
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable; // 内存自增
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; // 外设字节宽度
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte; // 内存字节宽度
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;              // 循环模式
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh; // 很高优先级
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable; // 关闭 M2M
    
    DMA_Init(USART_DMA_RX_CHANNEL, &DMA_InitStructure); // 初始化 RX DMA
    
    // 使能接收完成和半传输中断
    DMA_ITConfig(USART_DMA_RX_CHANNEL, DMA_IT_TC | DMA_IT_HT, ENABLE); // 使能 TC/HT 中断
    
    // 使能DMA接收通道
    DMA_Cmd(USART_DMA_RX_CHANNEL, ENABLE); // 使能 RX DMA
}

/**
    * @brief  初始化USART3 DMA
    * @retval None
    */
static void USART3_DMA_DMA_Init(void)
{
    DMA_InitTypeDef DMA_InitStructure; // DMA 配置结构体

        /* =============== DMA发送配置 =============== */
        DMA_DeInit(USART3_DMA_TX_CHANNEL); // 复位 TX DMA

        DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART3_DMA_USARTx->DR; // 外设地址
        DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)husart3.tx_buffer; // 内存地址
        DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST; // 内存到外设
        DMA_InitStructure.DMA_BufferSize = 0; // 初始为0
        DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable; // 外设不自增
        DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable; // 内存自增
        DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; // 外设字节宽度
        DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte; // 内存字节宽度
        DMA_InitStructure.DMA_Mode = DMA_Mode_Normal; // 正常模式
        DMA_InitStructure.DMA_Priority = DMA_Priority_High; // 高优先级
        DMA_InitStructure.DMA_M2M = DMA_M2M_Disable; // 关闭 M2M

        DMA_Init(USART3_DMA_TX_CHANNEL, &DMA_InitStructure); // 初始化 TX DMA

        // 使能发送完成中断
        DMA_ITConfig(USART3_DMA_TX_CHANNEL, DMA_IT_TC, ENABLE); // 使能发送完成中断

        /* =============== DMA接收配置 =============== */
        DMA_DeInit(USART3_DMA_RX_CHANNEL); // 复位 RX DMA

        DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART3_DMA_USARTx->DR; // 外设地址
        DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)husart3.rx_buffer; // 内存地址
        DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC; // 外设到内存
        DMA_InitStructure.DMA_BufferSize = USART_DMA_RX_BUFFER_SIZE; // 缓冲区大小
        DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable; // 外设不自增
        DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable; // 内存自增
        DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; // 外设字节宽度
        DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte; // 内存字节宽度
        DMA_InitStructure.DMA_Mode = DMA_Mode_Circular; // 循环模式
        DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh; // 很高优先级
        DMA_InitStructure.DMA_M2M = DMA_M2M_Disable; // 关闭 M2M

        DMA_Init(USART3_DMA_RX_CHANNEL, &DMA_InitStructure); // 初始化 RX DMA

        // 使能接收完成和半传输中断
        DMA_ITConfig(USART3_DMA_RX_CHANNEL, DMA_IT_TC | DMA_IT_HT, ENABLE); // 使能 TC/HT 中断

        // 使能DMA接收通道
        DMA_Cmd(USART3_DMA_RX_CHANNEL, ENABLE); // 使能 RX DMA
    }

/**
  * @brief  初始化NVIC中断
  * @retval None
  */
static void USART_DMA_NVIC_Init(void)
{
    NVIC_InitTypeDef NVIC_InitStructure; // NVIC 配置结构体
    
    // DMA发送通道中断
    NVIC_InitStructure.NVIC_IRQChannel = USART_DMA_TX_IRQn; // TX 中断通道
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; // 抢占优先级
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1; // 响应优先级
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; // 使能中断
    NVIC_Init(&NVIC_InitStructure); // 初始化 NVIC
    
    // DMA接收通道中断
    NVIC_InitStructure.NVIC_IRQChannel = USART_DMA_RX_IRQn; // RX 中断通道
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; // 抢占优先级
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0; // 响应优先级
    NVIC_Init(&NVIC_InitStructure); // 初始化 NVIC
}

/**
    * @brief  初始化USART3 NVIC中断
    * @retval None
    */
static void USART3_DMA_NVIC_Init(void)
{
    NVIC_InitTypeDef NVIC_InitStructure; // NVIC 配置结构体

        // DMA发送通道中断
        NVIC_InitStructure.NVIC_IRQChannel = USART3_DMA_TX_IRQn; // TX 中断通道
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; // 抢占优先级
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1; // 响应优先级
        NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; // 使能中断
        NVIC_Init(&NVIC_InitStructure); // 初始化 NVIC

        // DMA接收通道中断
        NVIC_InitStructure.NVIC_IRQChannel = USART3_DMA_RX_IRQn; // RX 中断通道
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; // 抢占优先级
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0; // 响应优先级
        NVIC_Init(&NVIC_InitStructure); // 初始化 NVIC
    }

/* =============== 数据发送函数 =============== */

/**
  * @brief  通过DMA发送数据
  * @param  data: 要发送的数据指针
  * @param  length: 数据长度
  * @retval USART_DMA_Status
  */
USART_DMA_Status USART_DMA_SendData(uint8_t *data, uint16_t length)
{
    uint32_t timeout = 1000000UL;                                    // 超时计数器（约1秒 @72MHz）

    if (length == 0) { // 长度为 0
        return USART_DMA_ERROR; // 返回错误
    }
    
    if (length > USART_DMA_TX_BUFFER_SIZE) { // 超过缓冲区
        return USART_DMA_BUFFER_FULL; // 返回满
    }
    
    // 等待之前的发送完成（带超时保护）
    while (husart1.tx_busy)
    {
        if (--timeout == 0U)                                        // 超时退出，避免死循环
        {
            DMA_Cmd(USART_DMA_TX_CHANNEL, DISABLE);                  // 强制关闭DMA
            husart1.tx_busy = false;                                  // 复位忙标志
            return USART_DMA_TIMEOUT;                                 // 返回超时错误
        }
    }
    
    // 复制数据到发送缓冲区
    memcpy(husart1.tx_buffer, data, length); // 复制数据
    
    // 禁用DMA通道
    DMA_Cmd(USART_DMA_TX_CHANNEL, DISABLE); // 关闭 TX DMA
    
    // 配置DMA传输参数
    USART_DMA_TX_CHANNEL->CNDTR = length;                     // 传输数量
    USART_DMA_TX_CHANNEL->CMAR = (uint32_t)husart1.tx_buffer; // 内存地址
    
    // 清除传输完成标志
    DMA_ClearFlag(USART_DMA_TX_TC_FLAG); // 清标志
    
    // 设置忙标志
    husart1.tx_busy = true; // 置忙
    
    // 使能DMA通道
    DMA_Cmd(USART_DMA_TX_CHANNEL, ENABLE); // 使能 TX DMA
    
    return USART_DMA_OK; // 返回成功
}

/**
  * @brief  发送字符串
  * @param  str: 要发送的字符串
  * @retval USART_DMA_Status
  */
USART_DMA_Status USART_DMA_SendString(const char *str)
{
    uint16_t length = 0; // 字符串长度
    
    if (str == NULL) { // 空指针
        return USART_DMA_ERROR; // 返回错误
    }
    
    // 计算字符串长度
    while (str[length] != '\0') { // 遍历字符串
        length++; // 长度加一
        if (length >= USART_DMA_TX_BUFFER_SIZE) { // 达到上限
            length = USART_DMA_TX_BUFFER_SIZE; // 截断长度
            break; // 退出循环
        }
    }
    
    if (length == 0) { // 空字符串
        return USART_DMA_OK; // 直接返回
    }
    
    return USART_DMA_SendData((uint8_t *)str, length); // 发送字符串
}

/**
  * @brief  中断方式发送数据（非阻塞）
  * @param  data: 要发送的数据指针
  * @param  length: 数据长度
  * @retval USART_DMA_Status
  */
USART_DMA_Status USART_DMA_SendData_IT(uint8_t *data, uint16_t length)
{
    if (husart1.tx_busy) { // 正在发送
        return USART_DMA_BUSY; // 返回忙
    }
    
    return USART_DMA_SendData(data, length); // 直接调用发送
}

USART_DMA_Status USART3_DMA_SendData(uint8_t *data, uint16_t length)
{
    uint32_t timeout = 1000000UL;                                    // 超时计数器（约1秒 @72MHz）

    if (length == 0) { // 长度为 0
        return USART_DMA_ERROR; // 返回错误
    }

    if (length > USART_DMA_TX_BUFFER_SIZE) { // 超过缓冲区
        return USART_DMA_BUFFER_FULL; // 返回满
    }

    // 等待之前的发送完成（带超时保护）
    while (husart3.tx_busy)
    {
        if (--timeout == 0U)                                        // 超时退出，避免死循环
        {
            DMA_Cmd(USART3_DMA_TX_CHANNEL, DISABLE);                  // 强制关闭DMA
            husart3.tx_busy = false;                                  // 复位忙标志
            return USART_DMA_TIMEOUT;                                 // 返回超时错误
        }
    }

    // 复制数据到发送缓冲区
    memcpy(husart3.tx_buffer, data, length); // 复制数据

    // 禁用DMA通道
    DMA_Cmd(USART3_DMA_TX_CHANNEL, DISABLE); // 关闭 TX DMA

    // 配置DMA传输参数
    USART3_DMA_TX_CHANNEL->CNDTR = length; // 传输数量
    USART3_DMA_TX_CHANNEL->CMAR = (uint32_t)husart3.tx_buffer; // 内存地址

    // 清除传输完成标志
    DMA_ClearFlag(USART3_DMA_TX_TC_FLAG); // 清标志

    // 设置忙标志
    husart3.tx_busy = true; // 置忙

    // 使能DMA通道
    DMA_Cmd(USART3_DMA_TX_CHANNEL, ENABLE); // 使能 TX DMA

    return USART_DMA_OK; // 返回成功
}

USART_DMA_Status USART3_DMA_SendString(const char *str)
{
    uint16_t length = 0; // 字符串长度

    if (str == NULL) { // 空指针
        return USART_DMA_ERROR; // 返回错误
    }

    // 计算字符串长度
    while (str[length] != '\0') { // 遍历字符串
        length++; // 长度加一
        if (length >= USART_DMA_TX_BUFFER_SIZE) { // 达到上限
            length = USART_DMA_TX_BUFFER_SIZE; // 截断长度
            break; // 退出循环
        }
    }

    if (length == 0) { // 空字符串
        return USART_DMA_OK; // 直接返回
    }

    return USART3_DMA_SendData((uint8_t *)str, length); // 发送字符串
}

USART_DMA_Status USART3_DMA_SendData_IT(uint8_t *data, uint16_t length)
{
    if (husart3.tx_busy) { // 正在发送
        return USART_DMA_BUSY; // 返回忙
    }

    return USART3_DMA_SendData(data, length); // 直接调用发送
}

/* =============== 数据接收函数 =============== */

/**
  * @brief  读取接收到的数据
  * @param  buffer: 接收缓冲区
  * @param  max_len: 最大接收长度
  * @retval 实际读取的数据长度
  */
uint16_t USART_DMA_ReceiveData(uint8_t *buffer, uint16_t max_len)
{
    uint16_t read_count = 0; // 实际读取数
    uint16_t available = USART_DMA_Available(); // 可用字节数
    
    if (available == 0 || max_len == 0 || buffer == NULL) { // 参数检查
        return 0; // 无数据
    }
    
    // 计算要读取的数据量
    read_count = (available > max_len) ? max_len : available; // 取较小值
    
    // 复制数据
    for (uint16_t i = 0; i < read_count; i++) { // 逐字节复制
        buffer[i] = husart1.rx_buffer[husart1.rx_read_pos]; // 取字节
        husart1.rx_read_pos = (husart1.rx_read_pos + 1) % USART_DMA_RX_BUFFER_SIZE; // 更新读指针
    }
    
    // 更新可用数据量
    husart1.rx_available -= read_count; // 更新可用字节
    
    return read_count; // 返回读取数
}

uint16_t USART3_DMA_ReceiveData(uint8_t *buffer, uint16_t max_len)
{
    uint16_t read_count = 0; // 实际读取数
    uint16_t available = USART3_DMA_Available(); // 可用字节数

    if (available == 0 || max_len == 0 || buffer == NULL) { // 参数检查
        return 0; // 无数据
    }

    read_count = (available > max_len) ? max_len : available; // 取较小值

    for (uint16_t i = 0; i < read_count; i++) { // 逐字节复制
        buffer[i] = husart3.rx_buffer[husart3.rx_read_pos]; // 取字节
        husart3.rx_read_pos = (husart3.rx_read_pos + 1) % USART_DMA_RX_BUFFER_SIZE; // 更新读指针
    }

    husart3.rx_available -= read_count; // 更新可用字节

    return read_count; // 返回读取数
}

/**
  * @brief  获取可用的接收数据字节数
  * @retval 可用的字节数
  */
uint16_t USART_DMA_Available(void)
{
    // 获取DMA当前的传输计数
    uint16_t dma_count = USART_DMA_RX_CHANNEL->CNDTR; // 读取计数器
    
    // 计算DMA已经接收的数据量
    uint16_t dma_received = USART_DMA_RX_BUFFER_SIZE - dma_count; // 已接收字节
    
    // 计算缓冲区中的有效数据
    if (dma_received >= husart1.rx_read_pos) { // 未回绕
        husart1.rx_available = dma_received - husart1.rx_read_pos; // 直接相减
    } else { // 已回绕
        husart1.rx_available = USART_DMA_RX_BUFFER_SIZE - husart1.rx_read_pos + dma_received; // 回绕计算
    }
    
    return husart1.rx_available; // 返回可用字节
}

uint16_t USART3_DMA_Available(void)
{
    uint16_t dma_count = USART3_DMA_RX_CHANNEL->CNDTR; // 读取计数器
    uint16_t dma_received = USART_DMA_RX_BUFFER_SIZE - dma_count; // 已接收字节

    if (dma_received >= husart3.rx_read_pos) { // 未回绕
        husart3.rx_available = dma_received - husart3.rx_read_pos; // 直接相减
    } else { // 已回绕
        husart3.rx_available = USART_DMA_RX_BUFFER_SIZE - husart3.rx_read_pos + dma_received; // 回绕计算
    }

    return husart3.rx_available; // 返回可用字节
}

/**
  * @brief  清空接收缓冲区
  * @retval None
  */
void USART_DMA_ClearRxBuffer(void)
{
    // 禁用DMA接收通道
    DMA_Cmd(USART_DMA_RX_CHANNEL, DISABLE); // 关闭 RX DMA
    
    // 重置DMA传输计数
    USART_DMA_RX_CHANNEL->CNDTR = USART_DMA_RX_BUFFER_SIZE; // 重置计数
    
    // 重置缓冲区索引
    husart1.rx_read_pos = 0; // 读指针归零
    husart1.rx_write_pos = 0; // 写指针归零
    husart1.rx_available = 0; // 可用字节清零
    
    // 重新使能DMA接收通道
    DMA_Cmd(USART_DMA_RX_CHANNEL, ENABLE); // 使能 RX DMA
}

void USART3_DMA_ClearRxBuffer(void)
{
    DMA_Cmd(USART3_DMA_RX_CHANNEL, DISABLE); // 关闭 RX DMA

    USART3_DMA_RX_CHANNEL->CNDTR = USART_DMA_RX_BUFFER_SIZE; // 重置计数

    husart3.rx_read_pos = 0; // 读指针归零
    husart3.rx_write_pos = 0; // 写指针归零
    husart3.rx_available = 0; // 可用字节清零

    DMA_Cmd(USART3_DMA_RX_CHANNEL, ENABLE); // 使能 RX DMA
}

/**
  * @brief  读取单个字节
  * @retval 读取的字节，如果没有数据返回0
  */
uint8_t USART_DMA_ReadByte(void)
{
    uint8_t data = 0; // 返回字节
    
    if (USART_DMA_Available() > 0) { // 有数据
        data = husart1.rx_buffer[husart1.rx_read_pos]; // 读取字节
        husart1.rx_read_pos = (husart1.rx_read_pos + 1) % USART_DMA_RX_BUFFER_SIZE; // 更新读指针
        husart1.rx_available--; // 更新可用字节
    }
    
    return data; // 返回读取值
}

uint8_t USART3_DMA_ReadByte(void)
{
    uint8_t data = 0; // 返回字节

    if (USART3_DMA_Available() > 0) { // 有数据
        data = husart3.rx_buffer[husart3.rx_read_pos]; // 读取字节
        husart3.rx_read_pos = (husart3.rx_read_pos + 1) % USART_DMA_RX_BUFFER_SIZE; // 更新读指针
        husart3.rx_available--; // 更新可用字节
    }

    return data; // 返回读取值
}

/**
  * @brief  在接收缓冲区中查找字符串
  * @param  str: 要查找的字符串
  * @param  position: 找到的位置（可选）
  * @retval 是否找到
  */
bool USART_DMA_FindString(const char *str, uint16_t *position)
{
    uint16_t available = USART_DMA_Available(); // 可用字节数
    uint16_t str_len = strlen(str); // 字符串长度
    
    if (str_len == 0 || available < str_len) { // 无法匹配
        return false; // 返回失败
    }
    
    // 在环形缓冲区中查找
    for (uint16_t i = 0; i <= available - str_len; i++) { // 遍历起点
        bool found = true; // 假设找到
        
        for (uint16_t j = 0; j < str_len; j++) { // 遍历字符串
            uint16_t index = (husart1.rx_read_pos + i + j) % USART_DMA_RX_BUFFER_SIZE; // 计算索引
            if (husart1.rx_buffer[index] != str[j]) { // 字节不匹配
                found = false; // 标记失败
                break; // 退出内层
            }
        }
        
        if (found) { // 找到匹配
            if (position != NULL) { // 需要位置
                *position = i; // 回写位置
            }
            return true; // 返回成功
        }
    }
    
    return false; // 未找到
}

bool USART3_DMA_FindString(const char *str, uint16_t *position)
{
    uint16_t available = USART3_DMA_Available(); // 可用字节数
    uint16_t str_len = strlen(str); // 字符串长度

    if (str_len == 0 || available < str_len) { // 无法匹配
        return false; // 返回失败
    }

    for (uint16_t i = 0; i <= available - str_len; i++) { // 遍历起点
        bool found = true; // 假设找到

        for (uint16_t j = 0; j < str_len; j++) { // 遍历字符串
            uint16_t index = (husart3.rx_read_pos + i + j) % USART_DMA_RX_BUFFER_SIZE; // 计算索引
            if (husart3.rx_buffer[index] != str[j]) { // 字节不匹配
                found = false; // 标记失败
                break; // 退出内层
            }
        }

        if (found) { // 找到匹配
            if (position != NULL) { // 需要位置
                *position = i; // 回写位置
            }
            return true; // 返回成功
        }
    }

    return false; // 未找到
}

/* =============== 状态查询函数 =============== */

/**
  * @brief  检查发送是否忙
  * @retval true-忙, false-空闲
  */
bool USART_DMA_IsTxBusy(void)
{
    return husart1.tx_busy; // 返回发送忙状态
}

bool USART3_DMA_IsTxBusy(void)
{
    return husart3.tx_busy; // 返回发送忙状态
}

/**
  * @brief  检查接收是否空闲
  * @retval true-空闲, false-忙
  */
bool USART_DMA_IsRxIdle(void)
{
    return husart1.rx_idle; // 返回接收空闲状态
}

bool USART3_DMA_IsRxIdle(void)
{
    return husart3.rx_idle; // 返回接收空闲状态
}

/**
  * @brief  获取发送缓冲区剩余空间
  * @retval 剩余空间字节数
  */
uint16_t USART_DMA_GetTxBufferFree(void)
{
    return USART_DMA_TX_BUFFER_SIZE; // 返回发送缓冲可用量
}

uint16_t USART3_DMA_GetTxBufferFree(void)
{
    return USART_DMA_TX_BUFFER_SIZE; // 返回发送缓冲可用量
}

/**
  * @brief  获取接收缓冲区已使用空间
  * @retval 已使用字节数
  */
uint16_t USART_DMA_GetRxBufferUsed(void)
{
    return USART_DMA_Available(); // 返回接收缓冲已用量
}

uint16_t USART3_DMA_GetRxBufferUsed(void)
{
    return USART3_DMA_Available(); // 返回接收缓冲已用量
}

/* =============== 回调函数设置 =============== */

/**
  * @brief  设置接收回调函数
  * @param  callback: 回调函数指针
  * @retval None
  */
void USART_DMA_SetRxCallback(void (*callback)(uint8_t *data, uint16_t length))
{
    husart1.rx_callback = callback; // 设置接收回调
}

void USART3_DMA_SetRxCallback(void (*callback)(uint8_t *data, uint16_t length))
{
    husart3.rx_callback = callback; // 设置接收回调
}

/**
  * @brief  设置发送完成回调函数
  * @param  callback: 回调函数指针
  * @retval None
  */
void USART_DMA_SetTxCompleteCallback(void (*callback)(void))
{
    husart1.tx_complete_callback = callback; // 设置发送完成回调
}

void USART3_DMA_SetTxCompleteCallback(void (*callback)(void))
{
    husart3.tx_complete_callback = callback; // 设置发送完成回调
}

/* =============== 中断处理函数 =============== */

/**
  * @brief  DMA发送中断处理函数
  * @retval None
  */
void USART_DMA_TX_IRQHandler(void)
{
    if (DMA_GetITStatus(USART_DMA_TX_TC_FLAG) != RESET) { // 检查中断
        // 清除中断标志
        DMA_ClearITPendingBit(USART_DMA_TX_TC_FLAG); // 清标志
        
        // 禁用DMA通道
        DMA_Cmd(USART_DMA_TX_CHANNEL, DISABLE); // 关闭 TX DMA
        
        // 清除忙标志
        husart1.tx_busy = false; // 发送完成
        
        // 调用发送完成回调函数
        if (husart1.tx_complete_callback != NULL) { // 有回调
            husart1.tx_complete_callback(); // 调用回调
        }
    }
}

void USART3_DMA_TX_IRQHandler(void)
{
    if (DMA_GetITStatus(USART3_DMA_TX_TC_FLAG) != RESET) { // 检查中断
        DMA_ClearITPendingBit(USART3_DMA_TX_TC_FLAG); // 清标志

        DMA_Cmd(USART3_DMA_TX_CHANNEL, DISABLE); // 关闭 TX DMA

        husart3.tx_busy = false; // 发送完成

        if (husart3.tx_complete_callback != NULL) { // 有回调
            husart3.tx_complete_callback(); // 调用回调
        }
    }
}

/**
  * @brief  DMA接收中断处理函数
  * @retval None
  */
void USART_DMA_RX_IRQHandler(void)
{
    if (DMA_GetITStatus(USART_DMA_RX_TC_FLAG) != RESET) { // 检查 TC 中断
        // 清除中断标志
        DMA_ClearITPendingBit(USART_DMA_RX_TC_FLAG); // 清标志
        
        // 这里可以处理接收完成事件
        // 例如：设置接收完成标志
        husart1.rx_idle = false; // 标记接收中
        
        // 如果有回调函数，调用它
        if (husart1.rx_callback != NULL) { // 有回调
            uint16_t available = USART_DMA_Available(); // 可用字节
            if (available > 0) { // 有数据
                husart1.rx_callback(husart1.rx_buffer, available); // 回调
            }
        }
    }
    
    if (DMA_GetITStatus(USART_DMA_RX_HT_FLAG) != RESET) { // 检查 HT 中断
        // 清除中断标志
        DMA_ClearITPendingBit(USART_DMA_RX_HT_FLAG); // 清标志
        
        // 这里可以处理半传输完成事件
    }
}

void USART3_DMA_RX_IRQHandler(void)
{
    if (DMA_GetITStatus(USART3_DMA_RX_TC_FLAG) != RESET) { // 检查 TC 中断
        DMA_ClearITPendingBit(USART3_DMA_RX_TC_FLAG); // 清标志

        husart3.rx_idle = false; // 标记接收中

        if (husart3.rx_callback != NULL) { // 有回调
            uint16_t available = USART3_DMA_Available(); // 可用字节
            if (available > 0) { // 有数据
                husart3.rx_callback(husart3.rx_buffer, available); // 回调
            }
        }
    }

    if (DMA_GetITStatus(USART3_DMA_RX_HT_FLAG) != RESET) { // 检查 HT 中断
        DMA_ClearITPendingBit(USART3_DMA_RX_HT_FLAG); // 清标志
    }
}

/* =============== 辅助函数 =============== */

void UartInit(uint32_t Baudrate)
{
    USART3_DMA_Init(Baudrate); // 初始化 USART3
}

void SendChar(unsigned char t)
{
    uint32_t timeout = 1000000UL;                                    // 超时计数器
    uint8_t temp = (uint8_t)t; // 转换为 uint8_t
    USART3_DMA_SendData(&temp, 1); // 发送字节
    while (USART3_DMA_IsTxBusy())
    {
        if (--timeout == 0U)                                          // 超时保护
        {
            DMA_Cmd(USART3_DMA_TX_CHANNEL, DISABLE);
            husart3.tx_busy = false;
            break;
        }
    }
}
