/*
 * EasyLogger header for bare-metal STM32F103RC
 * Based on armink/EasyLogger (https://github.com/armink/EasyLogger)
 *
 * Licensed under MIT License.
 */

#ifndef _ELOG_H_
#define _ELOG_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* EasyLogger software version */
#define ELOG_SW_VERSION             "2.2.99"

/* log level */
#define ELOG_LVL_ASSERT             0
#define ELOG_LVL_ERROR              1
#define ELOG_LVL_WARN               2
#define ELOG_LVL_INFO               3
#define ELOG_LVL_DEBUG              4
#define ELOG_LVL_VERBOSE            5

/* output log level (need to be configured in elog_cfg.h) */
#ifndef ELOG_OUTPUT_LVL
#define ELOG_OUTPUT_LVL             ELOG_LVL_VERBOSE
#endif

/* default: use NULL for func when fmt->func is disabled */
#ifndef ELOG_OUTPUT_FUNC
#define ELOG_OUTPUT_FUNC            NULL
#endif

/* all formats index */
typedef enum {
    ELOG_FMT_LVL    = 1 << 0,     /* level */
    ELOG_FMT_TAG    = 1 << 1,     /* tag */
    ELOG_FMT_TIME   = 1 << 2,     /* current time */
    ELOG_FMT_P_INFO = 1 << 3,     /* process info */
    ELOG_FMT_T_INFO = 1 << 4,     /* thread info */
    ELOG_FMT_DIR    = 1 << 5,     /* file directory and name */
    ELOG_FMT_FUNC   = 1 << 6,     /* function name */
    ELOG_FMT_LINE   = 1 << 7,     /* line number */
    ELOG_FMT_ALL    = 0xFF,       /* all formats */
} ElogFmtIndex;

/* macro to combine multiple format flags */
#define ELOG_FMT_COMBINE(fmt, ...) (fmt | __VA_ARGS__)

/* error code */
typedef enum {
    ELOG_NO_ERR = 0,
    ELOG_ARG_ERR,
    ELOG_INIT_ERR,
} ElogErrCode;

/*---------------------------------------------------------------------------*/
/* User APIs — these macros expand to nothing when log level is filtered */
/*---------------------------------------------------------------------------*/

#if ELOG_OUTPUT_LVL >= ELOG_LVL_ASSERT
#define log_a(...)      elog_a(LOG_TAG, __VA_ARGS__)
#else
#define log_a(...)      ((void)0)
#endif

#if ELOG_OUTPUT_LVL >= ELOG_LVL_ERROR
#define log_e(...)      elog_e(LOG_TAG, __VA_ARGS__)
#else
#define log_e(...)      ((void)0)
#endif

#if ELOG_OUTPUT_LVL >= ELOG_LVL_WARN
#define log_w(...)      elog_w(LOG_TAG, __VA_ARGS__)
#else
#define log_w(...)      ((void)0)
#endif

#if ELOG_OUTPUT_LVL >= ELOG_LVL_INFO
#define log_i(...)      elog_i(LOG_TAG, __VA_ARGS__)
#else
#define log_i(...)      ((void)0)
#endif

#if ELOG_OUTPUT_LVL >= ELOG_LVL_DEBUG
#define log_d(...)      elog_d(LOG_TAG, __VA_ARGS__)
#else
#define log_d(...)      ((void)0)
#endif

#if ELOG_OUTPUT_LVL >= ELOG_LVL_VERBOSE
#define log_v(...)      elog_v(LOG_TAG, __VA_ARGS__)
#else
#define log_v(...)      ((void)0)
#endif

/*---------------------------------------------------------------------------*/
/* assert */
/*---------------------------------------------------------------------------*/
#ifdef ELOG_ASSERT_ENABLE
#define ELOG_ASSERT(EXPR)                                               \
    if (!(EXPR)) {                                                      \
        elog_a("elog", "(%s) has assert failed at %s:%ld.",             \
               #EXPR, __FUNCTION__, (long)__LINE__);                    \
        while (1);                                                      \
    }
#else
#define ELOG_ASSERT(EXPR)           ((void)0)
#endif

/*---------------------------------------------------------------------------*/
/* Core functions */
/*---------------------------------------------------------------------------*/

/* output API */
void elog_output(uint8_t level, const char *tag, const char *file,
                 const char *func, const long line, const char *format, ...);
void elog_raw(const char *format, ...);
void elog_hexdump(const char *name, uint8_t width, uint8_t *buf, uint16_t size);

#define elog_assert(tag, ...)       elog_output(ELOG_LVL_ASSERT,  tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__)
#define elog_error(tag, ...)        elog_output(ELOG_LVL_ERROR,   tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__)
#define elog_warn(tag, ...)         elog_output(ELOG_LVL_WARN,    tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__)
#define elog_info(tag, ...)         elog_output(ELOG_LVL_INFO,    tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__)
#define elog_debug(tag, ...)        elog_output(ELOG_LVL_DEBUG,   tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__)
#define elog_verbose(tag, ...)      elog_output(ELOG_LVL_VERBOSE, tag, __FILE__, ELOG_OUTPUT_FUNC, __LINE__, __VA_ARGS__)

#define elog_a  elog_assert
#define elog_e  elog_error
#define elog_w  elog_warn
#define elog_i  elog_info
#define elog_d  elog_debug
#define elog_v  elog_verbose

/* init / start / deinit */
ElogErrCode elog_init(void);
void elog_start(void);
void elog_deinit(void);

/* setting */
void elog_set_output_enabled(bool enabled);
void elog_set_fmt(uint8_t level, size_t set);
void elog_set_filter_lvl(uint8_t level);
void elog_set_filter_tag(const char *tag);
void elog_set_filter_kw(const char *keyword);
void elog_set_filter_tag_lvl(const char *tag, uint8_t level);

/* async */
void elog_async_output(void);

#ifdef __cplusplus
}
#endif

#endif /* _ELOG_H_ */
