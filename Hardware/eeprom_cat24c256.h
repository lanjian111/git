#ifndef __EEPROM_CAT24C256_H /* 仅在尚未定义 __EEPROM_CAT24C256_H时编译下方代码。 */
#define __EEPROM_CAT24C256_H /* 定义 __EEPROM_CAT24C256_H，供头文件防重包含或后续条件编译检查。 */

#include "sys.h" /* 取得中断配置和系统基础类型声明。 */

/*==========================================================================
 * CAT24C256 EEPROM 驱动 (32KB, I2C, 64字节/页)
 *
 * 硬件连接:
 *   SCL → PB8,  SDA → PB9
 *   A0/A1/A2 → GND  →  设备地址 0x50
 *   WP → GND (始终允许写入)
 *
 * 设计说明:
 *   - 读取接口同步执行软件 I2C，写接口先同步发送第一页，后续页由任务发送。
 *   - EEPROM_Task 由主循环的 100 ms 周期任务调用。每页至少等待 5 ms 再推进，
 *     实际推进取决于调度。DONE 和 NACK 都需要调用方清状态。
 *==========================================================================*/

/* --- 设备参数 --- */
#define CAT24C256_ADDR          0x50U       /* 7 位地址 0x50。线上地址字节为写 0xA0、读 0xA1。 */
#define CAT24C256_PAGE_SIZE     64U         /* 页大小: 64 字节                        */
#define CAT24C256_CAPACITY      32768U      /* 总容量: 32768 字节 (32KB)              */
#define CAT24C256_ADDR_MASK     0x7FFFU     /* 15位地址掩码                           */
#define CAT24C256_WRITE_CYCLE_MS 5U         /* 内部写入周期: 5ms                      */

/* --- 状态机状态 --- */
typedef enum { /* 定义枚举的取值集合，供相关接口复用。 */
    EEPROM_STATE_IDLE       = 0,    /* 空闲，可以发起新操作                          */
    EEPROM_STATE_SEND_PAGE  = 1,    /* 页数据已发送，下次任务调用时开始计时。 */
    EEPROM_STATE_WAIT_WRITE = 2,    /* 等待 EEPROM 内部写入完成 (5ms)                */
    EEPROM_STATE_DONE       = 3,    /* 操作已完成                                    */
    EEPROM_STATE_NACK       = 4     /* I2C 无应答，设备异常                          */
} EEPROM_State_t; /* 为上述定义命名为 EEPROM_State_t，便于在声明中引用。 */

/* --- 操作结果 --- */
typedef enum { /* 定义枚举的取值集合，供相关接口复用。 */
    EEPROM_OK       = 0,    /* 参数已接受且尝试发送第一页，实际结果用 GetState 查询。 */
    EEPROM_BUSY     = 1,    /* 驱动处于非 IDLE，包括未清除的 DONE/NACK。 */
    EEPROM_ERROR    = 2     /* 参数错误 (地址越界/长度为零)                        */
} EEPROM_Result_t; /* 为上述定义命名为 EEPROM_Result_t，便于在声明中引用。 */

/*==========================================================================
 * 初始化
 *==========================================================================*/
void EEPROM_Init(void);                                 /* 初始化 I2C 底层和状态机     */

/*==========================================================================
 * 同步读取。起始地址按 0x7FFF 掩码。ReadByte 遇到 NACK 返回 0xFF，
 * ReadBuffer 此时不写输出。用 EEPROM_ReadHadNack 区分。len 为 0 时保留上次标志。
 *==========================================================================*/
uint8_t  EEPROM_ReadByte(uint16_t addr);                /* 读取单字节                  */
void     EEPROM_ReadBuffer(uint16_t addr, /* 声明接口，EEPROM 读取 缓冲区。 */
                           uint8_t *buf, /* 定义缓冲区字段，供这组配置或数据结构使用。 */
                           uint16_t len);               /* 连续读取 len 字节到 buf     */

/*==========================================================================
 * 写入接口先同步发送第一页，写周期等待和后续页由 EEPROM_Task 推进。
 * 跨页写入期间保留源缓冲区。只有 IDLE 接收新写入请求。
 *==========================================================================*/
EEPROM_Result_t EEPROM_WriteByte(uint16_t addr, /* 声明接口，EEPROM 写入 字节。 */
                                  uint8_t data);        /* 异步写入单字节              */

EEPROM_Result_t EEPROM_WritePage(uint16_t addr, /* 声明接口，EEPROM 写入 Page。 */
                                  const uint8_t *buf, /* 定义缓冲区字段，供这组配置或数据结构使用。 */
                                  uint16_t len);        /* 异步写入一页 (len ≤ 64)     */

EEPROM_Result_t EEPROM_WriteBuffer(uint16_t addr, /* 声明接口，EEPROM 写入 缓冲区。 */
                                    const uint8_t *buf, /* 定义缓冲区字段，供这组配置或数据结构使用。 */
                                    uint16_t len);      /* 异步写入任意长度 (自动拆页)  */

/*==========================================================================
 * 状态机驱动 & 状态查询 (每 100ms 调用)
 *==========================================================================*/
void          EEPROM_Task(void);                        /* 状态机推进函数              */
EEPROM_State_t EEPROM_GetState(void);                   /* 查询当前状态                */
uint8_t       EEPROM_IsIdle(void);                      /* 是否空闲 (可发起新操作)      */
void          EEPROM_ClearState(void);                  /* 清除 DONE/NACK 状态回到 IDLE */
uint8_t       EEPROM_ReadHadNack(void);                 /* 上次读取是否遇到 NACK         */

#endif /* __EEPROM_CAT24C256_H */ /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
