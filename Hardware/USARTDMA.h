#ifndef __USARTDMA_H // 头文件保护开始
#define __USARTDMA_H // 头文件保护宏

#include "stm32f10x.h" // STM32 标准外设头
#include "misc.h" // 中断和工具头
#include <stdbool.h> // bool 类型支持
#include <string.h> // 字符串函数

#ifdef __cplusplus // C++ 兼容判断
extern "C" { // C 语言链接
#endif // __cplusplus


/* =============== 配置参数定义 =============== */

// 缓冲区大小
#define USART_DMA_TX_BUFFER_SIZE     256 // 发送缓冲区大小
#define USART_DMA_RX_BUFFER_SIZE     256 // 接收缓冲区大小

// USART1 (PA9/PA10) — 日志输出 (EasyLogger)
#define USART_DMA_USARTx             USART1 // USART1 实例
#define USART_DMA_GPIOx              GPIOA // USART1 GPIO 端口
#define USART_DMA_TX_PIN             GPIO_Pin_9 // USART1 TX 引脚
#define USART_DMA_RX_PIN             GPIO_Pin_10 // USART1 RX 引脚

// USART2 (PA2/PA3) — HMI串口屏通信
#define USART2_DMA_USARTx            USART2 // USART2 实例
#define USART2_DMA_GPIOx             GPIOA // USART2 GPIO 端口
#define USART2_DMA_TX_PIN            GPIO_Pin_2 // USART2 TX 引脚
#define USART2_DMA_RX_PIN            GPIO_Pin_3 // USART2 RX 引脚

// DMA配置
#define USART_DMA_TX_CHANNEL         DMA1_Channel4 // USART1 TX DMA 通道
#define USART_DMA_RX_CHANNEL         DMA1_Channel5 // USART1 RX DMA 通道
#define USART_DMA_TX_TC_FLAG         DMA1_FLAG_TC4 // USART1 TX 完成标志
#define USART_DMA_RX_TC_FLAG         DMA1_FLAG_TC5 // USART1 RX 完成标志
#define USART_DMA_TX_HT_FLAG         DMA1_FLAG_HT4 // USART1 TX 半传标志
#define USART_DMA_RX_HT_FLAG         DMA1_FLAG_HT5 // USART1 RX 半传标志

// USART2 DMA配置
#define USART2_DMA_TX_CHANNEL        DMA1_Channel7 // USART2 TX DMA 通道
#define USART2_DMA_RX_CHANNEL        DMA1_Channel6 // USART2 RX DMA 通道
#define USART2_DMA_TX_TC_FLAG        DMA1_FLAG_TC7 // USART2 TX 完成标志
#define USART2_DMA_RX_TC_FLAG        DMA1_FLAG_TC6 // USART2 RX 完成标志
#define USART2_DMA_TX_HT_FLAG        DMA1_FLAG_HT7 // USART2 TX 半传标志
#define USART2_DMA_RX_HT_FLAG        DMA1_FLAG_HT6 // USART2 RX 半传标志

// 中断配置
#define USART_DMA_TX_IRQn            DMA1_Channel4_IRQn // USART1 TX DMA 中断号
#define USART_DMA_RX_IRQn            DMA1_Channel5_IRQn // USART1 RX DMA 中断号

// USART2 DMA中断配置
#define USART2_DMA_TX_IRQn           DMA1_Channel7_IRQn // USART2 TX DMA 中断号
#define USART2_DMA_RX_IRQn           DMA1_Channel6_IRQn // USART2 RX DMA 中断号

// 时钟配置
#define USART_DMA_GPIO_CLK           RCC_APB2Periph_GPIOA // USART1 GPIO 时钟
#define USART_DMA_USART_CLK          RCC_APB2Periph_USART1 // USART1 时钟
#define USART_DMA_DMA_CLK            RCC_AHBPeriph_DMA1 // DMA1 时钟

// USART2 时钟配置
#define USART2_DMA_GPIO_CLK          RCC_APB2Periph_GPIOA // USART2 GPIO 时钟
#define USART2_DMA_USART_CLK         RCC_APB1Periph_USART2 // USART2 时钟

/* =============== 类型定义 =============== */

// 错误状态枚举
typedef enum { // DMA 状态枚举
    USART_DMA_OK = 0, // 正常
    USART_DMA_ERROR, // 错误
    USART_DMA_BUSY, // 忙
    USART_DMA_TIMEOUT, // 超时
    USART_DMA_BUFFER_FULL // 缓冲区满
} USART_DMA_Status; // 枚举类型名

// 串口DMA句柄结构体
typedef struct { // DMA 句柄定义
    // 缓冲区
    uint8_t tx_buffer[USART_DMA_TX_BUFFER_SIZE]; // 发送缓冲区
    uint8_t rx_buffer[USART_DMA_RX_BUFFER_SIZE]; // 接收缓冲区
    
    // 状态标志
    volatile bool tx_busy; // 发送忙标志
    volatile bool rx_idle; // 接收空闲标志
    
    // 缓冲区索引
    uint16_t rx_write_pos; // 接收写指针
    uint16_t rx_read_pos; // 接收读指针
    uint16_t rx_available; // 可读字节数
    
    // 回调函数指针（可选）
    void (*rx_callback)(uint8_t *data, uint16_t length); // 接收回调
    void (*tx_complete_callback)(void); // 发送完成回调
} USART_DMA_HandleTypeDef; // 句柄类型名

/* =============== 全局变量声明 =============== */
extern USART_DMA_HandleTypeDef husart1; // USART1 句柄（日志）
extern USART_DMA_HandleTypeDef husart2; // USART2 句柄（屏幕）

/* =============== 函数声明 =============== */

// 初始化函数
void USART_DMA_Init(uint32_t baudrate); // 初始化 USART1 DMA（日志）
void USART_DMA_DeInit(void); // 反初始化 USART1 DMA（日志）

void USART2_DMA_Init(uint32_t baudrate); // 初始化 USART2 DMA（屏幕）
void USART2_DMA_DeInit(void); // 反初始化 USART2 DMA（屏幕）

// 数据发送函数
USART_DMA_Status USART_DMA_SendData(uint8_t *data, uint16_t length); // USART1 发送数据
USART_DMA_Status USART_DMA_SendString(const char *str); // USART1 发送字符串
USART_DMA_Status USART_DMA_SendData_IT(uint8_t *data, uint16_t length); // USART1 中断方式发送

USART_DMA_Status USART2_DMA_SendData(uint8_t *data, uint16_t length); // USART2 发送数据
USART_DMA_Status USART2_DMA_SendString(const char *str); // USART2 发送字符串
USART_DMA_Status USART2_DMA_SendData_IT(uint8_t *data, uint16_t length); // USART2 中断方式发送

// 数据接收函数
uint16_t USART_DMA_ReceiveData(uint8_t *buffer, uint16_t max_len); // USART1 读取接收数据
uint16_t USART_DMA_Available(void); // USART1 获取可读字节数
void USART_DMA_ClearRxBuffer(void); // 清空接收缓冲
uint8_t USART_DMA_ReadByte(void); // 读取单字节
bool USART_DMA_FindString(const char *str, uint16_t *position); // 查找字符串

uint16_t USART2_DMA_ReceiveData(uint8_t *buffer, uint16_t max_len); // USART2 读取接收数据
uint16_t USART2_DMA_Available(void); // USART2 可读字节数
void USART2_DMA_ClearRxBuffer(void); // USART2 清空接收缓冲
uint8_t USART2_DMA_ReadByte(void); // USART2 读取单字节
bool USART2_DMA_FindString(const char *str, uint16_t *position); // USART2 查找字符串

// 状态查询函数
bool USART_DMA_IsTxBusy(void); // USART1 查询发送忙
bool USART_DMA_IsRxIdle(void); // USART1 查询接收空闲
uint16_t USART_DMA_GetTxBufferFree(void); // USART1 获取发送缓冲可用量
uint16_t USART_DMA_GetRxBufferUsed(void); // USART1 获取接收缓冲已用量

bool USART2_DMA_IsTxBusy(void); // USART2 查询发送忙
bool USART2_DMA_IsRxIdle(void); // USART2 查询接收空闲
uint16_t USART2_DMA_GetTxBufferFree(void); // USART2 获取发送缓冲可用量
uint16_t USART2_DMA_GetRxBufferUsed(void); // USART2 获取接收缓冲已用量

// 回调函数设置
void USART_DMA_SetRxCallback(void (*callback)(uint8_t *data, uint16_t length)); // USART1 设置接收回调
void USART_DMA_SetTxCompleteCallback(void (*callback)(void)); // USART1 设置发送完成回调

void USART2_DMA_SetRxCallback(void (*callback)(uint8_t *data, uint16_t length)); // USART2 设置接收回调
void USART2_DMA_SetTxCompleteCallback(void (*callback)(void)); // USART2 设置发送完成回调

// 中断处理函数（需要在stm32f10x_it.c中调用）
void USART_DMA_TX_IRQHandler(void); // USART1 TX DMA 中断处理
void USART_DMA_RX_IRQHandler(void); // USART1 RX DMA 中断处理

void USART2_DMA_TX_IRQHandler(void); // USART2 TX DMA 中断处理
void USART2_DMA_RX_IRQHandler(void); // USART2 RX DMA 中断处理

// 辅助函数
// (USART_DMA_Printf / USART2_DMA_Printf 已移除，使用 EasyLogger 替代)

// HMI兼容接口（原hmi_user_uart）
void UartInit(uint32_t Baudrate); // HMI 初始化
void SendChar(unsigned char t); // HMI 发送字符

/* =============== 宏函数定义 =============== */

// 快速发送单个字符
#define USART_DMA_PutChar(ch) \
    do { \
        uint8_t temp = (uint8_t)(ch); \
        USART_DMA_SendData(&temp, 1); \
    } while(0) // 发送单字节

// 检查是否有数据可读
#define USART_DMA_HasData()          (USART_DMA_Available() > 0) // 判断可读

// 等待发送完成
#define USART_DMA_WaitTxComplete() \
    do { \
        while(USART_DMA_IsTxBusy()); \
    } while(0) // 等待发送结束



#endif /* __USART_DMA_H */ // 头文件保护结束
