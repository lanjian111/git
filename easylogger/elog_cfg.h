/*
 * EasyLogger configuration for STM32F103RC bare-metal project
 * Output: USART1 via DMA (async)
 */

#ifndef _ELOG_CFG_H_
#define _ELOG_CFG_H_

/*---------------------------------------------------------------------------*/
/* enable log output */
#define ELOG_OUTPUT_ENABLE

/*---------------------------------------------------------------------------*/
/* setting static output log level (range: ELOG_LVL_ASSERT ~ ELOG_LVL_VERBOSE) */
#define ELOG_OUTPUT_LVL              ELOG_LVL_VERBOSE

/* enable assert check */
#define ELOG_ASSERT_ENABLE

/*---------------------------------------------------------------------------*/
/* buffer size for every line's log */
#define ELOG_LINE_BUF_SIZE           128

/* output line number max length (0 = disable line number) */
#define ELOG_LINE_NUM_MAX_LEN        5

/* output filter's tag max length */
#define ELOG_FILTER_TAG_MAX_LEN      16

/* output filter's keyword max length */
#define ELOG_FILTER_KW_MAX_LEN       16

/* output filter's tag level max num */
#define ELOG_FILTER_TAG_LVL_MAX_NUM  5

/*---------------------------------------------------------------------------*/
/* output newline sign (CR+LF for Windows serial terminal) */
#define ELOG_NEWLINE_SIGN            "\r\n"

/*---------------------------------------------------------------------------*/
/* enable asynchronous output mode */
#define ELOG_ASYNC_OUTPUT_ENABLE

/* async output buffer size */
#define ELOG_ASYNC_OUTPUT_BUF_SIZE   1024

/* async output using line mode (each elog_output call = one line) */
#define ELOG_ASYNC_LINE_OUTPUT

/* polling interval (ms) for async output thread — not used in bare-metal */
#define ELOG_ASYNC_POLLING_INTERVAL  10

#endif /* _ELOG_CFG_H_ */
