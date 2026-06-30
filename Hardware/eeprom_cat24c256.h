#ifndef __EEPROM_CAT24C256_H
#define __EEPROM_CAT24C256_H

#include "sys.h"

/*==========================================================================
 * CAT24C256 EEPROM 驱动 (32KB, I?C, 64字节/页)
 *
 * 硬件连接:
 *   SCL → PB8,  SDA → PB9
 *   A0/A1/A2 → GND  →  设备地址 0x50
 *   WP → GND (始终允许写入)
 *
 * 设计说明:
 *   - 读取接口 (ReadByte/ReadBuffer): 同步阻塞，~15ms 读 256 字节
 *   - 写入接口 (WriteByte/WritePage/WriteBuffer): 异步非阻塞，状态机推进
 *   - EEPROM_Task(): 每 100ms 调用一次，驱动写入状态机
 *   - 5ms EEPROM 内部写入周期使用 delay_expired() 非阻塞等待
 *==========================================================================*/

/* --- 设备参数 --- */
#define CAT24C256_ADDR          0x50U       /* 7位设备地址 (1010 0000)                */
#define CAT24C256_PAGE_SIZE     64U         /* 页大小: 64 字节                        */
#define CAT24C256_CAPACITY      32768U      /* 总容量: 32768 字节 (32KB)              */
#define CAT24C256_ADDR_MASK     0x7FFFU     /* 15位地址掩码                           */
#define CAT24C256_WRITE_CYCLE_MS 5U         /* 内部写入周期: 5ms                      */

/* --- 状态机状态 --- */
typedef enum {
    EEPROM_STATE_IDLE       = 0,    /* 空闲，可以发起新操作                          */
    EEPROM_STATE_SEND_PAGE  = 1,    /* 正在发送一页数据到 EEPROM                     */
    EEPROM_STATE_WAIT_WRITE = 2,    /* 等待 EEPROM 内部写入完成 (5ms)                */
    EEPROM_STATE_DONE       = 3,    /* 操作已完成                                    */
    EEPROM_STATE_NACK       = 4     /* I?C 无应答，设备异常                          */
} EEPROM_State_t;

/* --- 操作结果 --- */
typedef enum {
    EEPROM_OK       = 0,    /* 操作已接受 (异步操作已入队)                          */
    EEPROM_BUSY     = 1,    /* 驱动器忙，上次操作尚未完成                          */
    EEPROM_ERROR    = 2     /* 参数错误 (地址越界/长度为零)                        */
} EEPROM_Result_t;

/*==========================================================================
 * 初始化
 *==========================================================================*/
void EEPROM_Init(void);                                 /* 初始化 I?C 底层和状态机     */

/*==========================================================================
 * 同步读取接口 (阻塞式，无 5ms 等待，直接返回)
 *==========================================================================*/
uint8_t  EEPROM_ReadByte(uint16_t addr);                /* 读取单字节                  */
void     EEPROM_ReadBuffer(uint16_t addr,
                           uint8_t *buf,
                           uint16_t len);               /* 连续读取 len 字节到 buf     */

/*==========================================================================
 * 异步写入接口 (立即返回，由 EEPROM_Task 后台完成)
 *==========================================================================*/
EEPROM_Result_t EEPROM_WriteByte(uint16_t addr,
                                  uint8_t data);        /* 异步写入单字节              */

EEPROM_Result_t EEPROM_WritePage(uint16_t addr,
                                  const uint8_t *buf,
                                  uint16_t len);        /* 异步写入一页 (len ≤ 64)     */

EEPROM_Result_t EEPROM_WriteBuffer(uint16_t addr,
                                    const uint8_t *buf,
                                    uint16_t len);      /* 异步写入任意长度 (自动拆页)  */

/*==========================================================================
 * 状态机驱动 & 状态查询 (每 100ms 调用)
 *==========================================================================*/
void          EEPROM_Task(void);                        /* 状态机推进函数              */
EEPROM_State_t EEPROM_GetState(void);                   /* 查询当前状态                */
uint8_t       EEPROM_IsIdle(void);                      /* 是否空闲 (可发起新操作)      */
void          EEPROM_ClearState(void);                  /* 清除 DONE/NACK 状态回到 IDLE */
uint8_t       EEPROM_ReadHadNack(void);                 /* 上次读取是否遇到 NACK         */

#endif /* __EEPROM_CAT24C256_H */
