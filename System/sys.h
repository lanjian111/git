#ifndef __SYS_H /* 仅在尚未定义 __SYS_H时编译下方代码。 */
#define __SYS_H /* 定义 __SYS_H，供头文件防重包含或后续条件编译检查。 */

#include "stm32f10x.h" /* 使用 STM32F1 的寄存器、外设类型和基于工程配置启用的驱动接口。 */
#include "string.h" /* 取得定长内存拷贝、填充、比较和字符串处理接口。 */
#include <stdbool.h> /* 使用布尔类型和真假常量表达状态。 */
#include <stdio.h> /* 取得 snprintf 等受缓冲区容量约束的格式化接口。 */
#include <stdlib.h> /* 取得标准数值转换及其他通用接口。 */
#include <math.h> /* 引入 math.h 的声明和配置，供当前文件调用。 */

/* Cortex-M3 位带别名访问。将 SRAM 或外设位带区的一位映射为一个 32 位地址，
 * 通过别名地址按位读写。这里主要用于软件 I2C 直接控制 GPIO。 */
#define BITBAND(addr, bitnum) ((addr & 0xF0000000)+0x2000000+((addr &0xFFFFF)<<5)+(bitnum<<2))  /* 将 BITBAND(addr, bitnum) 展开为 「((addr & 0xF0000000)+0x2000000+((addr &0xFFFFF)<<5)+(bitnum<<2))」，供调用处复用同一操作。 */
#define MEM_ADDR(addr)  *((volatile unsigned long  *)(addr))  /* 将 MEM_ADDR(addr) 展开为 「*((volatile unsigned long *)(addr))」，供调用处复用同一操作。 */
#define BIT_ADDR(addr, bitnum)   MEM_ADDR(BITBAND(addr, bitnum))  /* 将 BIT_ADDR(addr, bitnum) 展开为 「MEM_ADDR(BITBAND(addr, bitnum))」，供调用处复用同一操作。 */

/* STM32F1 GPIO 寄存器偏移，ODR 为输出数据，IDR 为输入数据。 */
#define GPIOA_ODR_Addr    (GPIOA_BASE+12) //0x4001080C  输出数据寄存器相对端口基址偏移 12 字节，供位带访问单个输出引脚。
#define GPIOB_ODR_Addr    (GPIOB_BASE+12) //0x40010C0C  输出数据寄存器相对端口基址偏移 12 字节，供位带访问单个输出引脚。
#define GPIOC_ODR_Addr    (GPIOC_BASE+12) //0x4001100C  输出数据寄存器相对端口基址偏移 12 字节，供位带访问单个输出引脚。
#define GPIOD_ODR_Addr    (GPIOD_BASE+12) //0x4001140C  输出数据寄存器相对端口基址偏移 12 字节，供位带访问单个输出引脚。
#define GPIOE_ODR_Addr    (GPIOE_BASE+12) //0x4001180C  输出数据寄存器相对端口基址偏移 12 字节，供位带访问单个输出引脚。
#define GPIOF_ODR_Addr    (GPIOF_BASE+12) //0x40011A0C     输出数据寄存器相对端口基址偏移 12 字节，供位带访问单个输出引脚。
#define GPIOG_ODR_Addr    (GPIOG_BASE+12) //0x40011E0C     输出数据寄存器相对端口基址偏移 12 字节，供位带访问单个输出引脚。

#define GPIOA_IDR_Addr    (GPIOA_BASE+8) //0x40010808  输入数据寄存器相对端口基址偏移 8 字节，供位带读取单个输入引脚。
#define GPIOB_IDR_Addr    (GPIOB_BASE+8) //0x40010C08  输入数据寄存器相对端口基址偏移 8 字节，供位带读取单个输入引脚。
#define GPIOC_IDR_Addr    (GPIOC_BASE+8) //0x40011008  输入数据寄存器相对端口基址偏移 8 字节，供位带读取单个输入引脚。
#define GPIOD_IDR_Addr    (GPIOD_BASE+8) //0x40011408  输入数据寄存器相对端口基址偏移 8 字节，供位带读取单个输入引脚。
#define GPIOE_IDR_Addr    (GPIOE_BASE+8) //0x40011808  输入数据寄存器相对端口基址偏移 8 字节，供位带读取单个输入引脚。
#define GPIOF_IDR_Addr    (GPIOF_BASE+8) //0x40011A08  输入数据寄存器相对端口基址偏移 8 字节，供位带读取单个输入引脚。
#define GPIOG_IDR_Addr    (GPIOG_BASE+8) //0x40011E08  输入数据寄存器相对端口基址偏移 8 字节，供位带读取单个输入引脚。
 
/* n 为单个引脚编号 0~15。输出宏操作 ODR，输入宏读取 IDR。
 * 使用前须开启对应 GPIO 时钟并配置引脚模式。 */
#define PAout(n)   BIT_ADDR(GPIOA_ODR_Addr,n)  //输出 
#define PAin(n)    BIT_ADDR(GPIOA_IDR_Addr,n)  //输入 
#define PBout(n)   BIT_ADDR(GPIOB_ODR_Addr,n)  //输出 
#define PBin(n)    BIT_ADDR(GPIOB_IDR_Addr,n)  //输入 
#define PCout(n)   BIT_ADDR(GPIOC_ODR_Addr,n)  //输出 
#define PCin(n)    BIT_ADDR(GPIOC_IDR_Addr,n)  //输入 
#define PDout(n)   BIT_ADDR(GPIOD_ODR_Addr,n)  //输出 
#define PDin(n)    BIT_ADDR(GPIOD_IDR_Addr,n)  //输入 
#define PEout(n)   BIT_ADDR(GPIOE_ODR_Addr,n)  //输出 
#define PEin(n)    BIT_ADDR(GPIOE_IDR_Addr,n)  //输入
#define PFout(n)   BIT_ADDR(GPIOF_ODR_Addr,n)  //输出 
#define PFin(n)    BIT_ADDR(GPIOF_IDR_Addr,n)  //输入
#define PGout(n)   BIT_ADDR(GPIOG_ODR_Addr,n)  //输出 
#define PGin(n)    BIT_ADDR(GPIOG_IDR_Addr,n)  //输入


// 联合体，数据转换两字节
typedef union  /* 定义关联字段的结构类型，供相关接口复用。 */
{
	uint8_t 				U8_Buff[2]; /* 定义U8_Buff[2]字段，供这组配置或数据结构使用。 */
	uint16_t 				U16; /* 定义U16字段，供这组配置或数据结构使用。 */
	int16_t 				S16; /* 定义S16字段，供这组配置或数据结构使用。 */
}Bint16_Union; /* 为上述定义命名为 Bint16_Union，便于在声明中引用。 */
/* 按目标机原生字节序共享 4 字节表示，使用方负责协议字节序。 */
typedef union  /* 定义关联字段的结构类型，供相关接口复用。 */
{
	uint8_t 				U8_Buff[4]; /* 定义U8_Buff[4]字段，供这组配置或数据结构使用。 */
	float 					Float; /* 定义Float字段，供这组配置或数据结构使用。 */
	unsigned long 	U32; /* 定义U32字段，供这组配置或数据结构使用。 */
}Bint32_Union; /* 为上述定义命名为 Bint32_Union，便于在声明中引用。 */


void NVIC_Configuration(void);                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               /* 声明接口，设置 NVIC 优先级分组，使各外设的抢占和子优先级按同一规则解释。 */



#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
