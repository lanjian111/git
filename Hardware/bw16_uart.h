#ifndef BW16_UART_H
#define BW16_UART_H

#include <stdbool.h>
#include <stdint.h>

/* 主板接线：STM32F103RC 第 53 脚 PC12 为 UART5_TX，第 54 脚 PD2 为 UART5_RX。
 * 默认 115200，实际值须与板载 BW16 当前通信口配置一致。
 * MAX_WRITE 是空发送队列能接纳的最大字节数。队列中已有待发数据时，可用空间更少。
 */
#define BW16_UART_DEFAULT_BAUD 115200U
#define BW16_UART_MAX_WRITE    255U

/* 由主循环一侧调用这些接口，同一时刻使用一个调用者。
 * 初始化串口为 8N1、无硬件流控。有效参数返回 true，重新初始化会清掉收发队列
 * 和故障计数。超出 UART 分频范围时返回 false，保留此前的串口配置和队列。
 */
bool BW16_UART_Init(uint32_t baudrate);

/* 发送 length 个字节，支持任意二进制数据。返回 true 表示已全部拷贝进队列，
 * 调用者可以立即复用源缓冲区。实际发送随后在中断中完成。
 * 返回 false 时本次无字节入队。原因包括串口未启用、空间不足、非零长度配空指针。
 */
bool BW16_UART_Write(const uint8_t *data, uint16_t length);

/* 取出当前已缓冲的字节，最多写入 capacity 个字节，返回实际数量。
 * 返回 0 表示本次未取到数据。一条回包可能分多次读到，由上层按协议拼接。
 * 函数按字节复制，不自动添加字符串结束符。
 */
uint16_t BW16_UART_Read(uint8_t *data, uint16_t capacity);

/* 读取累计接收故障事件数，读取本身不清零。线路错误、硬件溢出和软件缓冲区写满
 * 都会计数。组帧过程中若读数有变化，该帧可能缺字节，上层应考虑重新同步。
 */
uint32_t BW16_UART_RxFaultCount(void);

#endif
