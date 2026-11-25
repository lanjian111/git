#ifndef __SOFT_I2C_H
#define __SOFT_I2C_H

#include "sys.h"

/*==========================================================================
 * 软件模拟 I2C 底层驱动
 *
 * 引脚定义:
 *   SCL → PB8
 *   SDA → PB9
 *
 * 特点:
 *   - 使用 PBout/PBin 位带宏，GPIO 开漏输出依赖外部上拉。
 *   - 每个时序步骤通过 DWT 忙等，频率随读写路径和软件开销变化。
 *   - 调用方组织 START、字节读写和 STOP，事务耗时随字节数增长。
 *==========================================================================*/

/* --- 软件 I2C 时序步骤使用的延时 --- */
#define SOFT_I2C_DELAY_US   5U          /* 单次延时 5 μs，部分步骤会累计多次 */

/* --- 函数声明 --- */
void     SoftI2C_Init(void);                           /* 初始化 PB8/PB9 为开漏输出 */
void     SoftI2C_Start(void);                          /* 发送起始信号 */
void     SoftI2C_Stop(void);                           /* 发送停止信号 */
uint8_t  SoftI2C_SendByte(uint8_t data);               /* 发送 1 字节，返回 0=ACK / 1=NACK */
uint8_t  SoftI2C_ReadByte(uint8_t ack);                /* 读取 1 字节，ack=0 发 ACK，ack=1 发 NACK */

#endif /* __SOFT_I2C_H */
