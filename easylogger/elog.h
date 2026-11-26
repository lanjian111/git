/*
 * EasyLogger header for bare-metal STM32F103RC
 * Based on armink/EasyLogger (https://github.com/armink/EasyLogger)
 *
 * Licensed under MIT License.
 */

#ifndef _ELOG_H_ /* 仅在尚未定义 _ELOG_H_时编译下方代码。 */
#define _ELOG_H_ /* 定义 _ELOG_H_，供头文件防重包含或后续条件编译检查。 */

#include <stdint.h> /* 使用位宽明确的整数类型，匹配寄存器宽度及协议字段。 */
#include <stdbool.h> /* 使用布尔类型和真假常量表达状态。 */
#include <stddef.h> /* 取得尺寸类型和空指针相关定义。 */

#ifdef __cplusplus /* 仅在已定义 __cplusplus时编译下方代码。 */
extern "C" { /* 以 C 链接规则公开这些接口，便于 C++ 调用同一目标文件。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

/* EasyLogger software version */
#define ELOG_SW_VERSION             "2.2.99" /* 将 ELOG_SW_VERSION 定义为 「"2.2.99"」，供本模块及引用方统一使用。 */

/* log level */
#define ELOG_LVL_ASSERT             0 /* 定义 日志 级别 ASSERT 的约定值 0，使对应判断或封装统一引用。 */
#define ELOG_LVL_ERROR              1 /* 定义 日志 级别 错误 的约定值 1，使对应判断或封装统一引用。 */
#define ELOG_LVL_WARN               2 /* 定义 日志 级别 WARN 的约定值 2，使对应判断或封装统一引用。 */
#define ELOG_LVL_INFO               3 /* 定义 日志 级别 INFO 的约定值 3，使对应判断或封装统一引用。 */
#define ELOG_LVL_DEBUG              4 /* 定义 日志 级别 DEBUG 的约定值 4，使对应判断或封装统一引用。 */
#define ELOG_LVL_VERBOSE            5 /* 定义 日志 级别 VERBOSE 的约定值 5，使对应判断或封装统一引用。 */

/* output log level (need to be configured in elog_cfg.h) */
#ifndef ELOG_OUTPUT_LVL /* 仅在尚未定义 ELOG_OUTPUT_LVL时编译下方代码。 */
#define ELOG_OUTPUT_LVL             ELOG_LVL_VERBOSE /* 将 ELOG_OUTPUT_LVL 定义为 「ELOG_LVL_VERBOSE」，供本模块及引用方统一使用。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

/* default: use NULL for func when fmt->func is disabled */
#ifndef ELOG_OUTPUT_FUNC /* 仅在尚未定义 ELOG_OUTPUT_FUNC时编译下方代码。 */
#define ELOG_OUTPUT_FUNC            NULL /* 将 ELOG_OUTPUT_FUNC 定义为 「NULL」，供本模块及引用方统一使用。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

/* all formats index */
typedef enum { /* 定义枚举的取值集合，供相关接口复用。 */
    ELOG_FMT_LVL    = 1 << 0,     /* level */ /* 将日志 格式 级别 更新为 「1 << 0,」。 */
    ELOG_FMT_TAG    = 1 << 1,     /* tag */ /* 将日志 格式 标签 更新为 「1 << 1,」。 */
    ELOG_FMT_TIME   = 1 << 2,     /* current time */ /* 将日志 格式 时间 更新为 「1 << 2,」。 */
    ELOG_FMT_P_INFO = 1 << 3,     /* process info */ /* 将日志 格式 P INFO 更新为 「1 << 3,」。 */
    ELOG_FMT_T_INFO = 1 << 4,     /* thread info */ /* 将日志 格式 T INFO 更新为 「1 << 4,」。 */
    ELOG_FMT_DIR    = 1 << 5,     /* file directory and name */ /* 将日志 格式 DIR 更新为 「1 << 5,」。 */
    ELOG_FMT_FUNC   = 1 << 6,     /* function name */ /* 将日志 格式 FUNC 更新为 「1 << 6,」。 */
    ELOG_FMT_LINE   = 1 << 7,     /* line number */ /* 将日志 格式 线条 更新为 「1 << 7,」。 */
    ELOG_FMT_ALL    = 0xFF,       /* all formats */ /* 将日志 格式 ALL 更新为 「0xFF,」。 */
} ElogFmtIndex; /* 为上述定义命名为 ElogFmtIndex，便于在声明中引用。 */

/* macro to combine multiple format flags */
#define ELOG_FMT_COMBINE(fmt, ...) (fmt | __VA_ARGS__) /* 封装日志 格式 COMBINE，由预处理器按当前日志配置展开。 */

/* error code */
typedef enum { /* 定义枚举的取值集合，供相关接口复用。 */
    ELOG_NO_ERR = 0, /* 将日志 NO 错误 更新为 「0,」。 */
    ELOG_ARG_ERR, /* 定义 ELOG_ARG_ERR 的枚举取值，供状态判断和消息分发使用。 */
    ELOG_INIT_ERR, /* 定义 ELOG_INIT_ERR 的枚举取值，供状态判断和消息分发使用。 */
} ElogErrCode; /* 为上述定义命名为 ElogErrCode，便于在声明中引用。 */

/*---------------------------------------------------------------------------*/
/* User APIs — these macros expand to nothing when log level is filtered */
/*---------------------------------------------------------------------------*/

#if ELOG_OUTPUT_LVL >= ELOG_LVL_ASSERT /* 仅在「ELOG_OUTPUT_LVL >= ELOG_LVL_ASSERT」 成立时编译下方代码。 */
#define log_a(...)      elog_a(LOG_TAG, __VA_ARGS__) /* 封装日志 a，由预处理器按当前日志配置展开。 */
#else /* 前面的编译条件不成立时，改用下方实现或配置。 */
#define log_a(...)      ((void)0) /* 封装日志 a，由预处理器按当前日志配置展开。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

#if ELOG_OUTPUT_LVL >= ELOG_LVL_ERROR /* 仅在「ELOG_OUTPUT_LVL >= ELOG_LVL_ERROR」 成立时编译下方代码。 */
#define log_e(...)      elog_e(LOG_TAG, __VA_ARGS__) /* 封装日志 e，由预处理器按当前日志配置展开。 */
#else /* 前面的编译条件不成立时，改用下方实现或配置。 */
#define log_e(...)      ((void)0) /* 封装日志 e，由预处理器按当前日志配置展开。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

#if ELOG_OUTPUT_LVL >= ELOG_LVL_WARN /* 仅在「ELOG_OUTPUT_LVL >= ELOG_LVL_WARN」 成立时编译下方代码。 */
#define log_w(...)      elog_w(LOG_TAG, __VA_ARGS__) /* 封装日志 w，由预处理器按当前日志配置展开。 */
#else /* 前面的编译条件不成立时，改用下方实现或配置。 */
#define log_w(...)      ((void)0) /* 封装日志 w，由预处理器按当前日志配置展开。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

#if ELOG_OUTPUT_LVL >= ELOG_LVL_INFO /* 仅在「ELOG_OUTPUT_LVL >= ELOG_LVL_INFO」 成立时编译下方代码。 */
#define log_i(...)      elog_i(LOG_TAG, __VA_ARGS__) /* 封装日志 i，由预处理器按当前日志配置展开。 */
#else /* 前面的编译条件不成立时，改用下方实现或配置。 */
#define log_i(...)      ((void)0) /* 封装日志 i，由预处理器按当前日志配置展开。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

#if ELOG_OUTPUT_LVL >= ELOG_LVL_DEBUG /* 仅在「ELOG_OUTPUT_LVL >= ELOG_LVL_DEBUG」 成立时编译下方代码。 */
#define log_d(...)      elog_d(LOG_TAG, __VA_ARGS__) /* 封装日志 d，由预处理器按当前日志配置展开。 */
#else /* 前面的编译条件不成立时，改用下方实现或配置。 */
#define log_d(...)      ((void)0) /* 封装日志 d，由预处理器按当前日志配置展开。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

#if ELOG_OUTPUT_LVL >= ELOG_LVL_VERBOSE /* 仅在「ELOG_OUTPUT_LVL >= ELOG_LVL_VERBOSE」 成立时编译下方代码。 */
#define log_v(...)      elog_v(LOG_TAG, __VA_ARGS__) /* 封装日志 v，由预处理器按当前日志配置展开。 */
#else /* 前面的编译条件不成立时，改用下方实现或配置。 */
#define log_v(...)      ((void)0) /* 封装日志 v，由预处理器按当前日志配置展开。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

/*---------------------------------------------------------------------------*/
/* assert */
/*---------------------------------------------------------------------------*/
#ifdef ELOG_ASSERT_ENABLE /* 仅在已定义 ELOG_ASSERT_ENABLE时编译下方代码。 */
#define ELOG_ASSERT(EXPR)                                                /* 封装日志 ASSERT，由预处理器按当前日志配置展开。 */ \
    if (!(EXPR)) {                                                       /* 只有断言表达式为假才报告失败信息并停机。 */ \
        elog_a("elog", "(%s) has assert failed at %s:%ld.",              /* 用 「elog_a("elog", "(%s) has assert failed at %s:%ld.",」 补足上行的表达式、缓冲区内容或接口参数。 */ \
               #EXPR, __FUNCTION__, (long)__LINE__);                     /* 将断言表达式转为文本，并附带函数名和源码行号，帮助定位断言失败。 */ \
        while (1);                                                       /* 断言失败后停在空循环，保留现场供调试器检查。 */ \
    }
#else /* 前面的编译条件不成立时，改用下方实现或配置。 */
#define ELOG_ASSERT(EXPR)           ((void)0) /* 封装日志 ASSERT，由预处理器按当前日志配置展开。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

/*---------------------------------------------------------------------------*/
/* Core functions */
/*---------------------------------------------------------------------------*/

/* output API */
void elog_output(uint8_t level, const char *tag, const char *file, /* 声明接口，按运行时开关、级别和过滤条件格式化日志，再交给端口层输出。 */
                 const char *func, const long line, const char *format, ...); /* 定义func字段，供这组配置或数据结构使用。 */
void elog_raw(const char *format, ...); /* 声明接口，格式化原样输出，供调用者直接控制发送的文本。 */
void elog_hexdump(const char *name, uint8_t width, uint8_t *buf, uint16_t size); /* 声明接口，逐行把缓冲区转换成十六进制文本，便于调试数据内容。 */

#define elog_assert(tag, ...)       elog_output(ELOG_LVL_ASSERT,  tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__) /* 将 elog_assert(tag, ...) 展开为 「elog_output(ELOG_LVL_ASSERT, tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__)」，供调用处复用同一操作。 */
#define elog_error(tag, ...)        elog_output(ELOG_LVL_ERROR,   tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__) /* 将 elog_error(tag, ...) 展开为 「elog_output(ELOG_LVL_ERROR, tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__)」，供调用处复用同一操作。 */
#define elog_warn(tag, ...)         elog_output(ELOG_LVL_WARN,    tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__) /* 将 elog_warn(tag, ...) 展开为 「elog_output(ELOG_LVL_WARN, tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__)」，供调用处复用同一操作。 */
#define elog_info(tag, ...)         elog_output(ELOG_LVL_INFO,    tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__) /* 将 elog_info(tag, ...) 展开为 「elog_output(ELOG_LVL_INFO, tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__)」，供调用处复用同一操作。 */
#define elog_debug(tag, ...)        elog_output(ELOG_LVL_DEBUG,   tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__) /* 将 elog_debug(tag, ...) 展开为 「elog_output(ELOG_LVL_DEBUG, tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__)」，供调用处复用同一操作。 */
#define elog_verbose(tag, ...)      elog_output(ELOG_LVL_VERBOSE, tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__) /* 将 elog_verbose(tag, ...) 展开为 「elog_output(ELOG_LVL_VERBOSE, tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__)」，供调用处复用同一操作。 */

#define elog_a  elog_assert /* 将 elog_a 定义为 「elog_assert」，供本模块及引用方统一使用。 */
#define elog_e  elog_error /* 将 elog_e 定义为 「elog_error」，供本模块及引用方统一使用。 */
#define elog_w  elog_warn /* 将 elog_w 定义为 「elog_warn」，供本模块及引用方统一使用。 */
#define elog_i  elog_info /* 将 elog_i 定义为 「elog_info」，供本模块及引用方统一使用。 */
#define elog_d  elog_debug /* 将 elog_d 定义为 「elog_debug」，供本模块及引用方统一使用。 */
#define elog_v  elog_verbose /* 将 elog_v 定义为 「elog_verbose」，供本模块及引用方统一使用。 */

/* init / start / deinit */
ElogErrCode elog_init(void); /* 声明接口，初始化日志状态和输出端口。 */
void elog_start(void); /* 声明接口，开启日志输出，使后续日志可以送往已配置端口。 */
void elog_deinit(void); /* 声明接口，停止日志输出并释放日志端口的相关状态。 */

/* setting */
void elog_set_output_enabled(bool enabled); /* 声明接口，更新运行时日志总开关，控制后续日志是否进入输出流程。 */
void elog_set_fmt(uint8_t level, size_t set); /* 声明接口，配置指定日志级别要显示的格式字段。 */
void elog_set_filter_lvl(uint8_t level); /* 声明接口，设置全局日志级别阈值，过滤比阈值更详细的日志。 */
void elog_set_filter_tag(const char *tag); /* 声明接口，设置全局标签子串，仅输出匹配标签的日志。 */
void elog_set_filter_kw(const char *keyword); /* 声明接口，设置日志正文关键字，缩小输出范围。 */
void elog_set_filter_tag_lvl(const char *tag, uint8_t level); /* 声明接口，设置指定标签的独立阈值，控制该标签输出的详细程度。 */

/* async */
void elog_async_output(void); /* 声明接口，保留异步输出入口，供日志接口兼容使用。 */

#ifdef __cplusplus /* 仅在已定义 __cplusplus时编译下方代码。 */
}
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

#endif /* _ELOG_H_ */ /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
