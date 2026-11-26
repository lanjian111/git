/*
 * EasyLogger core implementation (simplified for bare-metal STM32)
 * Based on armink/EasyLogger — MIT License
 */

#include "elog.h" /* 取得日志级别、格式及输出入口的声明。 */
#include "elog_cfg.h" /* 取得日志缓冲区容量及编译开关配置。 */
#include <string.h> /* 取得定长内存拷贝、填充、比较和字符串处理接口。 */
#include <stdarg.h> /* 取得读取可变参数列表的类型及宏。 */
#include <stdio.h> /* 取得 snprintf 等受缓冲区容量约束的格式化接口。 */

/* check mandatory config macros */
#if !defined(ELOG_OUTPUT_LVL) /* 仅在「!defined(ELOG_OUTPUT_LVL)」 成立时编译下方代码。 */
#error "Please define ELOG_OUTPUT_LVL in elog_cfg.h" /* 由预处理器按 「"Please define ELOG_OUTPUT_LVL in elog_cfg.h"」 执行 error 指令。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
#if !defined(ELOG_LINE_NUM_MAX_LEN) /* 仅在「!defined(ELOG_LINE_NUM_MAX_LEN)」 成立时编译下方代码。 */
#error "Please define ELOG_LINE_NUM_MAX_LEN in elog_cfg.h" /* 由预处理器按 「"Please define ELOG_LINE_NUM_MAX_LEN in elog_cfg.h"」 执行 error 指令。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
#if !defined(ELOG_LINE_BUF_SIZE) /* 仅在「!defined(ELOG_LINE_BUF_SIZE)」 成立时编译下方代码。 */
#error "Please define ELOG_LINE_BUF_SIZE in elog_cfg.h" /* 由预处理器按 「"Please define ELOG_LINE_BUF_SIZE in elog_cfg.h"」 执行 error 指令。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
#if !defined(ELOG_FILTER_TAG_MAX_LEN) /* 仅在「!defined(ELOG_FILTER_TAG_MAX_LEN)」 成立时编译下方代码。 */
#error "Please define ELOG_FILTER_TAG_MAX_LEN in elog_cfg.h" /* 由预处理器按 「"Please define ELOG_FILTER_TAG_MAX_LEN in elog_cfg.h"」 执行 error 指令。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
#if !defined(ELOG_FILTER_KW_MAX_LEN) /* 仅在「!defined(ELOG_FILTER_KW_MAX_LEN)」 成立时编译下方代码。 */
#error "Please define ELOG_FILTER_KW_MAX_LEN in elog_cfg.h" /* 由预处理器按 「"Please define ELOG_FILTER_KW_MAX_LEN in elog_cfg.h"」 执行 error 指令。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
#if !defined(ELOG_NEWLINE_SIGN) /* 仅在「!defined(ELOG_NEWLINE_SIGN)」 成立时编译下方代码。 */
#error "Please define ELOG_NEWLINE_SIGN in elog_cfg.h" /* 由预处理器按 「"Please define ELOG_NEWLINE_SIGN in elog_cfg.h"」 执行 error 指令。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

/* ANSI color escape sequences for terminal output */
#define ANSI_COLOR_RESET   "\033[0m" /* 将 ANSI_COLOR_RESET 定义为 「"\033[0m"」，供本模块及引用方统一使用。 */
#define ANSI_COLOR_RED     "\033[31m" /* 将 ANSI_COLOR_RED 定义为 「"\033[31m"」，供本模块及引用方统一使用。 */
#define ANSI_COLOR_YELLOW  "\033[33m" /* 将 ANSI_COLOR_YELLOW 定义为 「"\033[33m"」，供本模块及引用方统一使用。 */
#define ANSI_COLOR_GREEN   "\033[32m" /* 将 ANSI_COLOR_GREEN 定义为 「"\033[32m"」，供本模块及引用方统一使用。 */
#define ANSI_COLOR_CYAN    "\033[36m" /* 将 ANSI_COLOR_CYAN 定义为 「"\033[36m"」，供本模块及引用方统一使用。 */
#define ANSI_COLOR_WHITE   "\033[37m" /* 将 ANSI_COLOR_WHITE 定义为 「"\033[37m"」，供本模块及引用方统一使用。 */

/* internal structures */
typedef struct { /* 定义关联字段的结构类型，供相关接口复用。 */
    uint8_t level; /* 声明液位，为后续计算或读取保存中间值。 */
    char    tag[ELOG_FILTER_TAG_MAX_LEN + 1]; /* 分配 「tag[ELOG_FILTER_TAG_MAX_LEN + 1]」 数组，作为当前操作的数据缓冲区。 */
    char    keyword[ELOG_FILTER_KW_MAX_LEN + 1]; /* 分配 「keyword[ELOG_FILTER_KW_MAX_LEN + 1]」 数组，作为当前操作的数据缓冲区。 */
    struct { /* 将每个日志标签和该标签的级别阈值组成一条独立记录。 */
        char    tag[ELOG_FILTER_TAG_MAX_LEN + 1]; /* 分配 「tag[ELOG_FILTER_TAG_MAX_LEN + 1]」 数组，作为当前操作的数据缓冲区。 */
        uint8_t level; /* 声明液位，为后续计算或读取保存中间值。 */
    } tag_lvl[ELOG_FILTER_TAG_LVL_MAX_NUM]; /* 按配置上限分配标签阈值表，供日志过滤时逐条匹配。 */
    uint8_t tag_lvl_num; /* 声明标签 级别 num，为后续计算或读取保存中间值。 */
} ElogFilter; /* 为上述定义命名为 ElogFilter，便于在声明中引用。 */

typedef struct { /* 定义关联字段的结构类型，供相关接口复用。 */
    bool     init_ok; /* 声明初始化 ok，为后续计算或读取保存中间值。 */
    bool     output_enabled; /* 声明输出 enabled，为后续计算或读取保存中间值。 */
    uint8_t  fmt_set[ELOG_LVL_VERBOSE + 1]; /* 分配 「fmt_set[ELOG_LVL_VERBOSE + 1]」 数组，作为当前操作的数据缓冲区。 */
    bool     output_is_locked_before_enable; /* 声明输出 is locked before 启用，为后续计算或读取保存中间值。 */
    bool     output_is_locked_before_disable; /* 声明输出 is locked before 关闭，为后续计算或读取保存中间值。 */
    ElogFilter filter; /* 在日志全局状态中保存过滤条件，让后续输出统一使用。 */
} Elog; /* 为上述定义命名为 Elog，便于在声明中引用。 */

static Elog elog = { 0 }; /* 用 「static Elog elog = { 0 }」 补足上行的表达式、缓冲区内容或接口参数。 */

/* extern: port layer */
extern void elog_port_output(const char *log, size_t size); /* 声明接口，把日志内容复制为有终止符的字符串，再交给串口发送。 */
extern void elog_port_output_lock(void); /* 声明接口，屏蔽 USART1 TX DMA 完成中断，建立日志输出临界段。 */
extern void elog_port_output_unlock(void); /* 声明接口，恢复 USART1 TX DMA 完成中断，结束日志输出临界段。 */

/*---------------------------------------------------------------------------*/
/* utility — safe string copy with length tracking */
/*---------------------------------------------------------------------------*/
size_t elog_strcpy(size_t cur_len, char *dst, const char *src) /* 向日志缓冲区追加原始文本并返回累计长度，供下一段继续拼接。 */
{
    size_t n; /* 声明n，为后续计算或读取保存中间值。 */

    /* NULL check first to avoid crash on strlen(NULL) */
    if (dst == NULL || src == NULL) { /* 先检查源和目标指针，避免对空地址读字符串长度或复制数据。 */
        return cur_len; /* 将 「cur_len」 返回给调用者，供上层使用。 */
    }

    n = strlen(src); /* 取得待追加字符串的有效字节数，复制时不搬运其 NUL。 */
    memcpy(dst, src, n); /* 把有效文本追加到目的位置，最终输出函数统一处理终止符。 */
    return cur_len + n; /* 返回添加文本后的累计长度，供下一段确定追加位置。 */
}

/*---------------------------------------------------------------------------*/
/* filter helpers */
/*---------------------------------------------------------------------------*/
static uint8_t elog_get_filter_tag_lvl(const char *tag) /* 查找标签专用日志阈值，没有匹配条目时返回最宽松级别。 */
{
    uint8_t i; /* 声明遍历计数器，用于扫描当前数组或记录。 */
    ELOG_ASSERT(tag != NULL); /* 在使用日志标签前检查指针，断言启用时可提前发现错误调用。 */
    for (i = 0; i < elog.filter.tag_lvl_num; i++) { /* 从 「i = 0」 开始，在 「i < elog.filter.tag_lvl_num」 成立时逐项处理，每轮执行 「i++」。 */
        if (strcmp(tag, elog.filter.tag_lvl[i].tag) == 0) { /* 精确匹配当前标签，找到时读取或更新该标签专用的日志阈值。 */
            return elog.filter.tag_lvl[i].level; /* 将 「elog.filter.tag_lvl[i].level」 返回给调用者，供上层使用。 */
        }
    }
    return ELOG_LVL_VERBOSE; /* 将 「ELOG_LVL_VERBOSE」 返回给调用者，供上层使用。 */
}

static void elog_set_filter_tag_lvl_default(void) /* 初始化标签过滤表，建立日志系统的默认筛选规则。 */
{
    elog.filter.tag_lvl_num = 1; /* 将标签 级别 num 更新为 「1」。 */
    strncpy(elog.filter.tag_lvl[0].tag, "*", ELOG_FILTER_TAG_MAX_LEN); /* 调用 strncpy 处理 「elog.filter.tag_lvl[0].tag, "*", ELOG_FILTER_TAG_MAX_LEN」。 */
    elog.filter.tag_lvl[0].tag[ELOG_FILTER_TAG_MAX_LEN] = '\0'; /* 将标签 级别 更新为 「'\0'」。 */
    elog.filter.tag_lvl[0].level = ELOG_LVL_VERBOSE; /* 将标签 级别 更新为 「ELOG_LVL_VERBOSE」。 */
}

/*---------------------------------------------------------------------------*/
/* level string lookup */
/*---------------------------------------------------------------------------*/
static const char *elog_level_str(uint8_t level) /* 将数字日志等级转换成打印时使用的短文本。 */
{
    switch (level) { /* 按液位的取值选择对应的处理分支。 */
    case ELOG_LVL_ASSERT:  return "A"; /* 进入 ELOG_LVL_ASSERT 对应的消息或取值处理分支。 */
    case ELOG_LVL_ERROR:   return "E"; /* 进入 ELOG_LVL_ERROR 对应的消息或取值处理分支。 */
    case ELOG_LVL_WARN:    return "W"; /* 进入 ELOG_LVL_WARN 对应的消息或取值处理分支。 */
    case ELOG_LVL_INFO:    return "I"; /* 进入 ELOG_LVL_INFO 对应的消息或取值处理分支。 */
    case ELOG_LVL_DEBUG:   return "D"; /* 进入 ELOG_LVL_DEBUG 对应的消息或取值处理分支。 */
    case ELOG_LVL_VERBOSE: return "V"; /* 进入 ELOG_LVL_VERBOSE 对应的消息或取值处理分支。 */
    default:               return "?"; /* 处理没有被列出的其余取值。 */
    }
}

static const char *elog_color_str(uint8_t level) /* 根据日志级别选择终端颜色的转义序列。 */
{
    switch (level) { /* 按液位的取值选择对应的处理分支。 */
    case ELOG_LVL_ASSERT:  return ANSI_COLOR_RED; /* 进入 ELOG_LVL_ASSERT 对应的消息或取值处理分支。 */
    case ELOG_LVL_ERROR:   return ANSI_COLOR_RED; /* 进入 ELOG_LVL_ERROR 对应的消息或取值处理分支。 */
    case ELOG_LVL_WARN:    return ANSI_COLOR_YELLOW; /* 进入 ELOG_LVL_WARN 对应的消息或取值处理分支。 */
    case ELOG_LVL_INFO:    return ANSI_COLOR_GREEN; /* 进入 ELOG_LVL_INFO 对应的消息或取值处理分支。 */
    case ELOG_LVL_DEBUG:   return ANSI_COLOR_CYAN; /* 进入 ELOG_LVL_DEBUG 对应的消息或取值处理分支。 */
    case ELOG_LVL_VERBOSE: return ANSI_COLOR_WHITE; /* 进入 ELOG_LVL_VERBOSE 对应的消息或取值处理分支。 */
    default:               return ""; /* 处理没有被列出的其余取值。 */
    }
}

/*---------------------------------------------------------------------------*/
/* init / start */
/*---------------------------------------------------------------------------*/
ElogErrCode elog_init(void) /* 初始化日志状态和输出端口。 */
{
    extern ElogErrCode elog_port_init(void); /* 声明接口，初始化日志适配层，底层串口由主程序提前准备。 */

    if (elog.init_ok) /* 当 「elog.init_ok」 成立时处理下方分支。 */
        return ELOG_NO_ERR; /* 底层 USART1 DMA 已在主程序中配置完毕，这里直接报告日志端口初始化成功。 */

    if (elog_port_init() != ELOG_NO_ERR) /* 当 「elog_port_init() != ELOG_NO_ERR」 成立时处理下方分支。 */
        return ELOG_INIT_ERR; /* 将 「ELOG_INIT_ERR」 返回给调用者，供上层使用。 */

    elog.output_enabled = true; /* 将输出 enabled 更新为 成功或条件成立（true）。 */
    elog.filter.level   = ELOG_OUTPUT_LVL; /* 将液位 更新为 「ELOG_OUTPUT_LVL」。 */
    elog.filter.tag[0]  = '\0'; /* 将标签 更新为 「'\0'」。 */
    elog.filter.keyword[0] = '\0'; /* 将关键字 更新为 「'\0'」。 */
    elog_set_filter_tag_lvl_default(); /* 初始化标签过滤表，建立日志系统的默认筛选规则。 */

    /* default format: level + tag + time */
    elog.fmt_set[ELOG_LVL_ASSERT]  = ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME; /* 将格式 设置 更新为 「ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME」。 */
    elog.fmt_set[ELOG_LVL_ERROR]   = ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME; /* 将格式 设置 更新为 「ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME」。 */
    elog.fmt_set[ELOG_LVL_WARN]    = ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME; /* 将格式 设置 更新为 「ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME」。 */
    elog.fmt_set[ELOG_LVL_INFO]    = ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME; /* 将格式 设置 更新为 「ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME」。 */
    elog.fmt_set[ELOG_LVL_DEBUG]   = ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME; /* 将格式 设置 更新为 「ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME」。 */
    elog.fmt_set[ELOG_LVL_VERBOSE] = ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME; /* 将格式 设置 更新为 「ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME」。 */

    elog.init_ok = true; /* 将初始化 ok 更新为 成功或条件成立（true）。 */
    return ELOG_NO_ERR; /* 底层 USART1 DMA 已在主程序中配置完毕，这里直接报告日志端口初始化成功。 */
}

void elog_start(void) /* 开启日志输出，使后续日志可以送往已配置端口。 */
{
    /* reserved for future use — async thread start in RTOS */
}

void elog_deinit(void) /* 停止日志输出并释放日志端口的相关状态。 */
{
    extern void elog_port_deinit(void); /* 声明接口，提供日志退出入口，适配层采用静态资源而无额外释放步骤。 */
    elog_port_deinit(); /* 提供日志退出入口，适配层采用静态资源而无额外释放步骤。 */
    memset(&elog, 0, sizeof(elog)); /* 将指定字节范围统一填充值，建立缓冲区或结构体初始内容。 */
}

/*---------------------------------------------------------------------------*/
/* settings API */
/*---------------------------------------------------------------------------*/
void elog_set_output_enabled(bool enabled) /* 更新运行时日志总开关，控制后续日志是否进入输出流程。 */
{
    elog.output_enabled = enabled; /* 将输出 enabled 更新为 「enabled」。 */
}

void elog_set_fmt(uint8_t level, size_t set) /* 配置指定日志级别要显示的格式字段。 */
{
    if (level > ELOG_LVL_VERBOSE) return; /* 当 「level > ELOG_LVL_VERBOSE」 成立时，提前结束本次调用，其余处理留待后续任务或调用。 */
    elog.fmt_set[level] = (uint8_t)set; /* 将格式 设置 更新为 「(uint8_t)set」。 */
}

void elog_set_filter_lvl(uint8_t level) /* 设置全局日志级别阈值，过滤比阈值更详细的日志。 */
{
    if (level > ELOG_LVL_VERBOSE) return; /* 当 「level > ELOG_LVL_VERBOSE」 成立时，提前结束本次调用，其余处理留待后续任务或调用。 */
    elog.filter.level = level; /* 将液位 更新为 「level」。 */
}

void elog_set_filter_tag(const char *tag) /* 设置全局标签子串，仅输出匹配标签的日志。 */
{
    if (tag == NULL) { /* 当 「tag == NULL」 成立时处理下方分支。 */
        elog.filter.tag[0] = '\0'; /* 将标签 更新为 「'\0'」。 */
    } else { /* 前面的条件不满足时执行此分支。 */
        strncpy(elog.filter.tag, tag, ELOG_FILTER_TAG_MAX_LEN); /* 调用 strncpy 处理 「elog.filter.tag, tag, ELOG_FILTER_TAG_MAX_LEN」。 */
        elog.filter.tag[ELOG_FILTER_TAG_MAX_LEN] = '\0'; /* 将标签 更新为 「'\0'」。 */
    }
}

void elog_set_filter_kw(const char *keyword) /* 设置日志正文关键字，缩小输出范围。 */
{
    if (keyword == NULL) { /* 当 「keyword == NULL」 成立时处理下方分支。 */
        elog.filter.keyword[0] = '\0'; /* 将关键字 更新为 「'\0'」。 */
    } else { /* 前面的条件不满足时执行此分支。 */
        strncpy(elog.filter.keyword, keyword, ELOG_FILTER_KW_MAX_LEN); /* 调用 strncpy 处理 「elog.filter.keyword, keyword, ELOG_FILTER_KW_MAX_LEN」。 */
        elog.filter.keyword[ELOG_FILTER_KW_MAX_LEN] = '\0'; /* 将关键字 更新为 「'\0'」。 */
    }
}

void elog_set_filter_tag_lvl(const char *tag, uint8_t level) /* 设置指定标签的独立阈值，控制该标签输出的详细程度。 */
{
    uint8_t i; /* 声明遍历计数器，用于扫描当前数组或记录。 */
    ELOG_ASSERT(tag != NULL); /* 在使用日志标签前检查指针，断言启用时可提前发现错误调用。 */
    ELOG_ASSERT(level <= ELOG_LVL_VERBOSE); /* 调用 ELOG_ASSERT 处理 「level <= ELOG_LVL_VERBOSE」。 */

    for (i = 0; i < elog.filter.tag_lvl_num; i++) { /* 从 「i = 0」 开始，在 「i < elog.filter.tag_lvl_num」 成立时逐项处理，每轮执行 「i++」。 */
        if (strcmp(tag, elog.filter.tag_lvl[i].tag) == 0) { /* 精确匹配当前标签，找到时读取或更新该标签专用的日志阈值。 */
            elog.filter.tag_lvl[i].level = level; /* 将标签 级别 更新为 「level」。 */
            return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */
        }
    }
    if (elog.filter.tag_lvl_num < ELOG_FILTER_TAG_LVL_MAX_NUM) { /* 标签专用过滤表还有空位时才新增条目，避免越界写入。 */
        strncpy(elog.filter.tag_lvl[elog.filter.tag_lvl_num].tag, tag, ELOG_FILTER_TAG_MAX_LEN); /* 调用 strncpy 处理 「elog.filter.tag_lvl[elog.filter.tag_lvl_num].tag, tag, ELOG_FILTER_TAG_MAX_LEN」。 */
        elog.filter.tag_lvl[elog.filter.tag_lvl_num].level = level; /* 将标签 级别 更新为 「level」。 */
        elog.filter.tag_lvl_num++; /* 将标签 级别 num加一，推进处理进度。 */
    }
}

/*---------------------------------------------------------------------------*/
/* format helpers */
/*---------------------------------------------------------------------------*/
static bool get_fmt_enabled(uint8_t level, size_t bit) /* 检查此日志级别配置中是否包含指定格式字段。 */
{
    return (elog.fmt_set[level] & bit) != 0; /* 将 「(elog.fmt_set[level] & bit) != 0」 返回给调用者，供上层使用。 */
}

/*---------------------------------------------------------------------------*/
/* core output */
/*---------------------------------------------------------------------------*/
void elog_output(uint8_t level, const char *tag, const char *file, /* 按运行时开关、级别和过滤条件格式化日志，再交给端口层输出。 */
                 const char *func, const long line, const char *format, ...) /* 声明 「func, const long line, const char *format, ...)」，用于本函数的中间结果和循环控制。 */
{
    extern const char *elog_port_get_time(void); /* 声明接口，用系统运行毫秒数生成日志时间戳。 */
    extern const char *elog_port_get_p_info(void); /* 声明接口，提供进程信息字段，裸机工程返回空值。 */
    extern const char *elog_port_get_t_info(void); /* 声明接口，提供线程信息字段，裸机工程返回空值。 */

    char log_buf[ELOG_LINE_BUF_SIZE] = { 0 }; /* 定义日志 缓冲区，将日志 缓冲区 更新为 「{ 0 }」。 */
    size_t log_len = 0; /* 定义日志 长度，将日志 长度清零，使本轮从初始计数开始。 */
    va_list args; /* 声明args，为后续计算或读取保存中间值。 */

    ELOG_ASSERT(level <= ELOG_LVL_VERBOSE); /* 调用 ELOG_ASSERT 处理 「level <= ELOG_LVL_VERBOSE」。 */

    /* output enabled check */
    if (!elog.output_enabled) return; /* 日志输出关闭时直接返回，省去后续过滤、格式化和串口操作。 */

    /* level filter */
    if (level > elog.filter.level || level > elog_get_filter_tag_lvl(tag)) /* 同时按全局和标签级别筛选日志，超过任一阈值即跳过输出。 */
        return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */

    /* tag keyword filter */
    if (elog.filter.tag[0] != '\0' && !strstr(tag, elog.filter.tag)) /* 配置了标签过滤时只保留标签包含筛选字串的日志。 */
        return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */

    /* ANSI color prefix based on log level */
    {
        const char *color = elog_color_str(level); /* 定义颜色，将颜色 更新为 「elog_color_str(level)」。 */
        log_len = elog_strcpy(log_len, &log_buf[log_len], color); /* 将日志 长度 更新为 「elog_strcpy(log_len, &log_buf[log_len], color)」。 */
    }

    /* format: [level] */
    if (get_fmt_enabled(level, ELOG_FMT_LVL)) { /* 当 「get_fmt_enabled(level, ELOG_FMT_LVL)」 成立时处理下方分支。 */
        log_buf[log_len++] = '['; /* 将日志 缓冲区 更新为 「'['」。 */
        log_buf[log_len++] = elog_level_str(level)[0]; /* 将日志 缓冲区 更新为 「elog_level_str(level)[0]」。 */
        log_buf[log_len++] = '/'; /* 将日志 缓冲区 更新为 「'/'」。 */
    }

    /* format: tag */
    if (get_fmt_enabled(level, ELOG_FMT_TAG)) { /* 当 「get_fmt_enabled(level, ELOG_FMT_TAG)」 成立时处理下方分支。 */
        size_t tag_len = strlen(tag); /* 定义标签 长度，将标签 长度 更新为 「strlen(tag)」。 */
        memcpy(&log_buf[log_len], tag, tag_len); /* 按长度复制原始数据，适用于包含零字节的缓冲区。 */
        log_len += tag_len; /* 将日志 长度 累加 「tag_len」。 */
        if (get_fmt_enabled(level, ELOG_FMT_LVL)) { /* 当 「get_fmt_enabled(level, ELOG_FMT_LVL)」 成立时处理下方分支。 */
            log_buf[log_len++] = ' '; /* 将日志 缓冲区 更新为 「' '」。 */
        }
    }

    /* format: suffix ": " after level+tag */
    if (get_fmt_enabled(level, ELOG_FMT_LVL) || get_fmt_enabled(level, ELOG_FMT_TAG)) { /* 当 「get_fmt_enabled(level, ELOG_FMT_LVL) || get_fmt_enabled(level, ELOG_FMT_TAG)」 成立时处理下方分支。 */
        log_buf[log_len++] = ':'; /* 将日志 缓冲区 更新为 「':'」。 */
        log_buf[log_len++] = ' '; /* 将日志 缓冲区 更新为 「' '」。 */
    }

    /* format: time */
    if (get_fmt_enabled(level, ELOG_FMT_TIME)) { /* 当 「get_fmt_enabled(level, ELOG_FMT_TIME)」 成立时处理下方分支。 */
        const char *time_str = elog_port_get_time(); /* 定义时间 str，将时间 str 更新为 「elog_port_get_time()」。 */
        log_len = elog_strcpy(log_len, &log_buf[log_len], time_str); /* 将日志 长度 更新为 「elog_strcpy(log_len, &log_buf[log_len], time_str)」。 */
        log_buf[log_len++] = ' '; /* 将日志 缓冲区 更新为 「' '」。 */
    }

    /* format: func */
    if (get_fmt_enabled(level, ELOG_FMT_FUNC) && func != NULL) { /* 当 「get_fmt_enabled(level, ELOG_FMT_FUNC) && func != NULL」 成立时处理下方分支。 */
        log_len = elog_strcpy(log_len, &log_buf[log_len], func); /* 将日志 长度 更新为 「elog_strcpy(log_len, &log_buf[log_len], func)」。 */
        log_buf[log_len++] = ' '; /* 将日志 缓冲区 更新为 「' '」。 */
    }

    /* body: format + va_args */
    {
        size_t remaining = sizeof(log_buf) - log_len - 4; /* reserve for newline */ /* 定义remaining，将remaining 更新为 「sizeof(log_buf) - log_len - 4」。 */
        va_start(args, format); /* 初始化可变参数读取位置，供格式化函数依次取参。 */
        vsnprintf(&log_buf[log_len], remaining, format, args); /* 按可变参数列表格式化文本，并限制目标缓冲区写入范围。 */
        va_end(args); /* 结束对此可变参数列表的访问。 */
        log_len = strlen(log_buf); /* 将日志 长度 更新为 「strlen(log_buf)」。 */
    }

    /* append newline — insert ANSI reset before newline, then newline */
    {
        const char *nl = ELOG_NEWLINE_SIGN; /* 定义nl，将nl 更新为 「ELOG_NEWLINE_SIGN」。 */
        size_t nl_len = strlen(nl); /* 定义nl 长度，将nl 长度 更新为 「strlen(nl)」。 */
        const char *reset = ANSI_COLOR_RESET; /* 定义复位，将复位 更新为 「ANSI_COLOR_RESET」。 */
        size_t reset_len = strlen(reset); /* 定义复位 长度，将复位 长度 更新为 「strlen(reset)」。 */
        if (log_len + reset_len + nl_len < sizeof(log_buf)) { /* 计算颜色复位串和换行的总开销，只有缓冲区能容纳时才追加。 */
            memcpy(&log_buf[log_len], reset, reset_len); /* 按长度复制原始数据，适用于包含零字节的缓冲区。 */
            log_len += reset_len; /* 将日志 长度 累加 「reset_len」。 */
            memcpy(&log_buf[log_len], nl, nl_len); /* 按长度复制原始数据，适用于包含零字节的缓冲区。 */
            log_len += nl_len; /* 将日志 长度 累加 「nl_len」。 */
        }
    }

    /* output to port (with lock) */
    elog_port_output_lock(); /* 屏蔽 USART1 TX DMA 完成中断，建立日志输出临界段。 */
    if (log_len > 0) { /* 存在待发送内容时才进入输出临界段，避免提交空日志。 */
        elog_port_output(log_buf, log_len); /* 把日志内容复制为有终止符的字符串，再交给串口发送。 */
    }
    elog_port_output_unlock(); /* 恢复 USART1 TX DMA 完成中断，结束日志输出临界段。 */
}

void elog_raw(const char *format, ...) /* 格式化原样输出，供调用者直接控制发送的文本。 */
{
    char log_buf[ELOG_LINE_BUF_SIZE]; /* 分配 「log_buf[ELOG_LINE_BUF_SIZE]」 数组，作为当前操作的数据缓冲区。 */
    size_t log_len; /* 声明日志 长度，为后续计算或读取保存中间值。 */
    va_list args; /* 声明args，为后续计算或读取保存中间值。 */

    va_start(args, format); /* 初始化可变参数读取位置，供格式化函数依次取参。 */
    log_len = (size_t)vsnprintf(log_buf, sizeof(log_buf), format, args); /* 将日志 长度 更新为 「(size_t)vsnprintf(log_buf, sizeof(log_buf), format, args)」。 */
    va_end(args); /* 结束对此可变参数列表的访问。 */

    elog_port_output_lock(); /* 屏蔽 USART1 TX DMA 完成中断，建立日志输出临界段。 */
    if (log_len > 0) { /* 存在待发送内容时才进入输出临界段，避免提交空日志。 */
        elog_port_output(log_buf, log_len); /* 把日志内容复制为有终止符的字符串，再交给串口发送。 */
    }
    elog_port_output_unlock(); /* 恢复 USART1 TX DMA 完成中断，结束日志输出临界段。 */
}

/*---------------------------------------------------------------------------*/
/* async output — stub (bare-metal: direct output, no async thread)          */
/*---------------------------------------------------------------------------*/
void elog_async_output(void) /* 保留异步输出入口，供日志接口兼容使用。 */
{
    /* bare-metal: all output is synchronous, nothing to flush */
}

/*---------------------------------------------------------------------------*/
/* hexdump — stub (not yet implemented for bare-metal)                       */
/*---------------------------------------------------------------------------*/
void elog_hexdump(const char *name, uint8_t width, uint8_t *buf, uint16_t size) /* 逐行把缓冲区转换成十六进制文本，便于调试数据内容。 */
{
    (void)name; /* 显式忽略 「name」，表明此处有意不使用该值。 */
    (void)width; /* 显式忽略 「width」，表明此处有意不使用该值。 */
    (void)buf; /* 显式忽略 「buf」，表明此处有意不使用该值。 */
    (void)size; /* 显式忽略 「size」，表明此处有意不使用该值。 */
    /* reserved for future implementation */
}
