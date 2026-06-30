#ifndef __SOFT_I2C_H
#define __SOFT_I2C_H

#include "sys.h"

/*==========================================================================
 * 软件模拟 I?C 底层驱动
 *
 * 引脚定义:
 *   SCL → PB8
 *   SDA → PB9
 *
 * 特点:
 *   - 使用位带操作(PBout/PBin)，速度最快
 *   - 基于 delay_us() 实现 100kHz 标准模式时序
 *   - GPIO 配置为开漏输出，依赖外部上拉电阻
 *   - 本层函数均为阻塞式，单次事务约 200~500μs
 *==========================================================================*/

/* --- I?C 时序参数 (100kHz, T_SCL = 10μs) --- */
#define SOFT_I2C_DELAY_US   5U          /* 半周期延时 5μs (T_HIGH=5μs, T_LOW=5μs) */

/* --- 函数声明 --- */
void     SoftI2C_Init(void);                           /* 初始化 PB8/PB9 为开漏输出 */
void     SoftI2C_Start(void);                          /* 发送起始信号 */
void     SoftI2C_Stop(void);                           /* 发送停止信号 */
uint8_t  SoftI2C_SendByte(uint8_t data);               /* 发送 1 字节，返回 0=ACK / 1=NACK */
uint8_t  SoftI2C_ReadByte(uint8_t ack);                /* 读取 1 字节，ack=0 发 ACK，ack=1 发 NACK */

#endif /* __SOFT_I2C_H */
