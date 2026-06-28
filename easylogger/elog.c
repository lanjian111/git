/*
 * EasyLogger core implementation (simplified for bare-metal STM32)
 * Based on armink/EasyLogger — MIT License
 */

#include <elog.h>
#include <elog_cfg.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/* check mandatory config macros */
#if !defined(ELOG_OUTPUT_LVL)
#error "Please define ELOG_OUTPUT_LVL in elog_cfg.h"
#endif
#if !defined(ELOG_LINE_NUM_MAX_LEN)
#error "Please define ELOG_LINE_NUM_MAX_LEN in elog_cfg.h"
#endif
#if !defined(ELOG_LINE_BUF_SIZE)
#error "Please define ELOG_LINE_BUF_SIZE in elog_cfg.h"
#endif
#if !defined(ELOG_FILTER_TAG_MAX_LEN)
#error "Please define ELOG_FILTER_TAG_MAX_LEN in elog_cfg.h"
#endif
#if !defined(ELOG_FILTER_KW_MAX_LEN)
#error "Please define ELOG_FILTER_KW_MAX_LEN in elog_cfg.h"
#endif
#if !defined(ELOG_NEWLINE_SIGN)
#error "Please define ELOG_NEWLINE_SIGN in elog_cfg.h"
#endif

/* internal structures */
typedef struct {
    uint8_t level;
    char    tag[ELOG_FILTER_TAG_MAX_LEN + 1];
    char    keyword[ELOG_FILTER_KW_MAX_LEN + 1];
    struct {
        char    tag[ELOG_FILTER_TAG_MAX_LEN + 1];
        uint8_t level;
    } tag_lvl[ELOG_FILTER_TAG_LVL_MAX_NUM];
    uint8_t tag_lvl_num;
} ElogFilter;

typedef struct {
    bool     init_ok;
    bool     output_enabled;
    uint8_t  fmt_set[ELOG_LVL_VERBOSE + 1];
    bool     output_is_locked_before_enable;
    bool     output_is_locked_before_disable;
    ElogFilter filter;
} Elog;

static Elog elog = { 0 };

/* extern: port layer */
extern void elog_port_output(const char *log, size_t size);
extern void elog_port_output_lock(void);
extern void elog_port_output_unlock(void);

/*---------------------------------------------------------------------------*/
/* utility — safe string copy with length tracking */
/*---------------------------------------------------------------------------*/
size_t elog_strcpy(size_t cur_len, char *dst, const char *src)
{
    size_t n = strlen(src);
    /* use memcpy to avoid strcpy's unsafe behavior */
    if (dst != NULL && src != NULL) {
        memcpy(dst, src, n);
    }
    return cur_len + n;
}

/*---------------------------------------------------------------------------*/
/* filter helpers */
/*---------------------------------------------------------------------------*/
static uint8_t elog_get_filter_tag_lvl(const char *tag)
{
    uint8_t i;
    ELOG_ASSERT(tag != NULL);
    for (i = 0; i < elog.filter.tag_lvl_num; i++) {
        if (strcmp(tag, elog.filter.tag_lvl[i].tag) == 0) {
            return elog.filter.tag_lvl[i].level;
        }
    }
    return ELOG_LVL_VERBOSE;
}

static void elog_set_filter_tag_lvl_default(void)
{
    elog.filter.tag_lvl_num = 1;
    strcpy(elog.filter.tag_lvl[0].tag, "*");
    elog.filter.tag_lvl[0].level = ELOG_LVL_VERBOSE;
}

/*---------------------------------------------------------------------------*/
/* level string lookup */
/*---------------------------------------------------------------------------*/
static const char *elog_level_str(uint8_t level)
{
    switch (level) {
    case ELOG_LVL_ASSERT:  return "A";
    case ELOG_LVL_ERROR:   return "E";
    case ELOG_LVL_WARN:    return "W";
    case ELOG_LVL_INFO:    return "I";
    case ELOG_LVL_DEBUG:   return "D";
    case ELOG_LVL_VERBOSE: return "V";
    default:               return "?";
    }
}

/*---------------------------------------------------------------------------*/
/* init / start */
/*---------------------------------------------------------------------------*/
ElogErrCode elog_init(void)
{
    extern ElogErrCode elog_port_init(void);

    if (elog.init_ok)
        return ELOG_NO_ERR;

    if (elog_port_init() != ELOG_NO_ERR)
        return ELOG_INIT_ERR;

    elog.output_enabled = true;
    elog.filter.level   = ELOG_OUTPUT_LVL;
    elog.filter.tag[0]  = '\0';
    elog.filter.keyword[0] = '\0';
    elog_set_filter_tag_lvl_default();

    /* default format: level + tag + time */
    elog.fmt_set[ELOG_LVL_ASSERT]  = ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME;
    elog.fmt_set[ELOG_LVL_ERROR]   = ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME;
    elog.fmt_set[ELOG_LVL_WARN]    = ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME;
    elog.fmt_set[ELOG_LVL_INFO]    = ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME;
    elog.fmt_set[ELOG_LVL_DEBUG]   = ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME;
    elog.fmt_set[ELOG_LVL_VERBOSE] = ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME;

    elog.init_ok = true;
    return ELOG_NO_ERR;
}

void elog_start(void)
{
    /* reserved for future use — async thread start in RTOS */
}

void elog_deinit(void)
{
    extern void elog_port_deinit(void);
    elog_port_deinit();
    memset(&elog, 0, sizeof(elog));
}

/*---------------------------------------------------------------------------*/
/* settings API */
/*---------------------------------------------------------------------------*/
void elog_set_output_enabled(bool enabled)
{
    elog.output_enabled = enabled;
}

void elog_set_fmt(uint8_t level, size_t set)
{
    if (level > ELOG_LVL_VERBOSE) return;
    elog.fmt_set[level] = (uint8_t)set;
}

void elog_set_filter_lvl(uint8_t level)
{
    if (level > ELOG_LVL_VERBOSE) return;
    elog.filter.level = level;
}

void elog_set_filter_tag(const char *tag)
{
    if (tag == NULL) {
        elog.filter.tag[0] = '\0';
    } else {
        strncpy(elog.filter.tag, tag, ELOG_FILTER_TAG_MAX_LEN);
        elog.filter.tag[ELOG_FILTER_TAG_MAX_LEN] = '\0';
    }
}

void elog_set_filter_kw(const char *keyword)
{
    if (keyword == NULL) {
        elog.filter.keyword[0] = '\0';
    } else {
        strncpy(elog.filter.keyword, keyword, ELOG_FILTER_KW_MAX_LEN);
        elog.filter.keyword[ELOG_FILTER_KW_MAX_LEN] = '\0';
    }
}

void elog_set_filter_tag_lvl(const char *tag, uint8_t level)
{
    uint8_t i;
    ELOG_ASSERT(tag != NULL);
    ELOG_ASSERT(level <= ELOG_LVL_VERBOSE);

    for (i = 0; i < elog.filter.tag_lvl_num; i++) {
        if (strcmp(tag, elog.filter.tag_lvl[i].tag) == 0) {
            elog.filter.tag_lvl[i].level = level;
            return;
        }
    }
    if (elog.filter.tag_lvl_num < ELOG_FILTER_TAG_LVL_MAX_NUM) {
        strncpy(elog.filter.tag_lvl[elog.filter.tag_lvl_num].tag, tag, ELOG_FILTER_TAG_MAX_LEN);
        elog.filter.tag_lvl[elog.filter.tag_lvl_num].level = level;
        elog.filter.tag_lvl_num++;
    }
}

/*---------------------------------------------------------------------------*/
/* format helpers */
/*---------------------------------------------------------------------------*/
static bool get_fmt_enabled(uint8_t level, size_t bit)
{
    return (elog.fmt_set[level] & bit) != 0;
}

/*---------------------------------------------------------------------------*/
/* core output */
/*---------------------------------------------------------------------------*/
void elog_output(uint8_t level, const char *tag, const char *file,
                 const char *func, const long line, const char *format, ...)
{
    extern const char *elog_port_get_time(void);
    extern const char *elog_port_get_p_info(void);
    extern const char *elog_port_get_t_info(void);

    char log_buf[ELOG_LINE_BUF_SIZE] = { 0 };
    size_t log_len = 0;
    va_list args;

    ELOG_ASSERT(level <= ELOG_LVL_VERBOSE);

    /* output enabled check */
    if (!elog.output_enabled) return;

    /* level filter */
    if (level > elog.filter.level || level > elog_get_filter_tag_lvl(tag))
        return;

    /* tag keyword filter */
    if (elog.filter.tag[0] != '\0' && !strstr(tag, elog.filter.tag))
        return;

    /* format: [level] */
    if (get_fmt_enabled(level, ELOG_FMT_LVL)) {
        log_buf[log_len++] = '[';
        log_buf[log_len++] = elog_level_str(level)[0];
        log_buf[log_len++] = '/';
    }

    /* format: tag */
    if (get_fmt_enabled(level, ELOG_FMT_TAG)) {
        size_t tag_len = strlen(tag);
        memcpy(&log_buf[log_len], tag, tag_len);
        log_len += tag_len;
        if (get_fmt_enabled(level, ELOG_FMT_LVL)) {
            log_buf[log_len++] = ' ';
        }
    }

    /* format: ] */
    if (get_fmt_enabled(level, ELOG_FMT_LVL) || get_fmt_enabled(level, ELOG_FMT_TAG)) {
        log_buf[log_len++] = ':';
        log_buf[log_len++] = ' ';
    }

    /* format: time */
    if (get_fmt_enabled(level, ELOG_FMT_TIME)) {
        const char *time_str = elog_port_get_time();
        log_len = elog_strcpy(log_len, &log_buf[log_len], time_str);
        log_buf[log_len++] = ' ';
    }

    /* format: func */
    if (get_fmt_enabled(level, ELOG_FMT_FUNC) && func != NULL) {
        log_len = elog_strcpy(log_len, &log_buf[log_len], func);
        log_buf[log_len++] = ' ';
    }

    /* body: format + va_args */
    {
        size_t remaining = sizeof(log_buf) - log_len - 4; /* reserve for newline */
        va_start(args, format);
        vsnprintf(&log_buf[log_len], remaining, format, args);
        va_end(args);
        log_len = strlen(log_buf);
    }

    /* append newline */
    {
        const char *nl = ELOG_NEWLINE_SIGN;
        size_t nl_len = strlen(nl);
        if (log_len + nl_len < sizeof(log_buf)) {
            memcpy(&log_buf[log_len], nl, nl_len);
            log_len += nl_len;
        }
    }

    /* output to port (with lock) */
    elog_port_output_lock();
    if (log_len > 0) {
        elog_port_output(log_buf, log_len);
    }
    elog_port_output_unlock();
}

void elog_raw(const char *format, ...)
{
    char log_buf[ELOG_LINE_BUF_SIZE];
    size_t log_len;
    va_list args;

    va_start(args, format);
    log_len = (size_t)vsnprintf(log_buf, sizeof(log_buf), format, args);
    va_end(args);

    elog_port_output_lock();
    if (log_len > 0) {
        elog_port_output(log_buf, log_len);
    }
    elog_port_output_unlock();
}
