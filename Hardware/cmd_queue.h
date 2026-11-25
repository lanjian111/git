/*! 
 *  \file cmd_queue.h
 *  \brief 串口指令队列
 *  \version 1.0
 *  \date 2012-2015
 *  \copyright 广州大彩光电科技有限公司
 */

#ifndef _CMD_QUEUE
#define _CMD_QUEUE
#include "hmi_driver.h"

typedef unsigned char qdata;
typedef unsigned short qsize;

/*! 
 *  \brief  清空指令数据
 */
extern void queue_reset(void);

/*! 
 * \brief  添加指令数据
 * \details 接收队列容量为 QUEUE_MAX_SIZE - 1，满时丢弃新输入。
 *  \param  _data 指令数据
 */
extern void queue_push(qdata _data);

/*! 
 *  \brief  从指令队列中取出一条完整的指令
 *  \param  cmd 组帧输出缓冲区，分段接收期间须重复传入同一缓冲区
 *  \param  buf_len 指令接收缓存区大小
 *  \return 已提取帧的长度。未找到帧尾或启用 CRC 时校验失败均返回 0。
 *  \note  通过 EE 与 FF FC FF FF 分隔，不解析长度字段。超出 buf_len 的字节仅参与帧尾检测。
 */
extern qsize queue_find_cmd(qdata *cmd,qsize buf_len);

#endif
