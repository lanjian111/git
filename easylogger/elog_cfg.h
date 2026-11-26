/*
 * EasyLogger configuration for STM32F103RC bare-metal project
 * Output: USART1 via DMA (async)
 */

#ifndef _ELOG_CFG_H_ /* 仅在尚未定义 _ELOG_CFG_H_时编译下方代码。 */
#define _ELOG_CFG_H_ /* 定义 _ELOG_CFG_H_，供头文件防重包含或后续条件编译检查。 */

/*---------------------------------------------------------------------------*/
/* enable log output */
#define ELOG_OUTPUT_ENABLE /* 定义 ELOG_OUTPUT_ENABLE，供头文件防重包含或后续条件编译检查。 */

/*---------------------------------------------------------------------------*/
/* setting static output log level (range: ELOG_LVL_ASSERT ~ ELOG_LVL_VERBOSE) */
#define ELOG_OUTPUT_LVL              ELOG_LVL_VERBOSE /* 将 ELOG_OUTPUT_LVL 定义为 「ELOG_LVL_VERBOSE」，供本模块及引用方统一使用。 */

/* enable assert check */
#define ELOG_ASSERT_ENABLE /* 定义 ELOG_ASSERT_ENABLE，供头文件防重包含或后续条件编译检查。 */

/*---------------------------------------------------------------------------*/
/* 普通日志行的格式化缓冲区，含前缀和行尾占用。长 HMI 十六进制日志可能被截断。 */
#define ELOG_LINE_BUF_SIZE           128 /* 以 128 定义日志 线条 缓冲区 尺寸，让分配容量和边界检查使用同一值。 */

/* output line number max length (0 = disable line number) */
#define ELOG_LINE_NUM_MAX_LEN        5 /* 定义 日志 线条 NUM 上限 长度 的约定值 5，使对应判断或封装统一引用。 */

/* output filter's tag max length */
#define ELOG_FILTER_TAG_MAX_LEN      16 /* 定义 日志 过滤 标签 上限 长度 的约定值 16，使对应判断或封装统一引用。 */

/* output filter's keyword max length */
#define ELOG_FILTER_KW_MAX_LEN       16 /* 定义 日志 过滤 关键字 上限 长度 的约定值 16，使对应判断或封装统一引用。 */

/* output filter's tag level max num */
#define ELOG_FILTER_TAG_LVL_MAX_NUM  5 /* 定义 日志 过滤 标签 级别 上限 NUM 的约定值 5，使对应判断或封装统一引用。 */

/*---------------------------------------------------------------------------*/
/* output newline sign (CR+LF for Windows serial terminal) */
#define ELOG_NEWLINE_SIGN            "\r\n" /* 将 ELOG_NEWLINE_SIGN 定义为 「"\r\n"」，供本模块及引用方统一使用。 */

/*---------------------------------------------------------------------------*/
/* 以下异步日志队列配置为兼容保留。当前 elog.c 直接调用输出端口，
 * USART1 DMA 负责后台发送已提交的数据。 */
#define ELOG_ASYNC_OUTPUT_ENABLE /* 定义 ELOG_ASYNC_OUTPUT_ENABLE，供头文件防重包含或后续条件编译检查。 */

/* async output buffer size */
#define ELOG_ASYNC_OUTPUT_BUF_SIZE   1024 /* 以 1024 定义日志 ASYNC 输出 缓冲区 尺寸，让分配容量和边界检查使用同一值。 */

/* async output using line mode (each elog_output call = one line) */
#define ELOG_ASYNC_LINE_OUTPUT /* 定义 ELOG_ASYNC_LINE_OUTPUT，供头文件防重包含或后续条件编译检查。 */

/* polling interval (ms) for async output thread — not used in bare-metal */
#define ELOG_ASYNC_POLLING_INTERVAL  10 /* 定义 日志 ASYNC POLLING 间隔 的约定值 10，使对应判断或封装统一引用。 */

#endif /* _ELOG_CFG_H_ */ /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
