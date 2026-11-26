/*! 
 *  \file cmd_queue.h
 *  \brief 串口指令队列
 *  \version 1.0
 *  \date 2012-2015
 *  \copyright 广州大彩光电科技有限公司
 */

#ifndef _CMD_QUEUE /* 仅在尚未定义 _CMD_QUEUE时编译下方代码。 */
#define _CMD_QUEUE /* 定义 _CMD_QUEUE，供头文件防重包含或后续条件编译检查。 */
#include "hmi_driver.h" /* 取得触摸屏命令封装和链路处理接口。 */

typedef unsigned char qdata; /* 声明类型别名 qdata，供其后的接口和变量统一使用。 */
typedef unsigned short qsize; /* 声明类型别名 qsize，供其后的接口和变量统一使用。 */

/*! 
 *  \brief  清空指令数据
 */
extern void queue_reset(void); /* 声明接口，同时清空环形队列和跨调用保留的帧匹配进度。 */

/*! 
 * \brief  添加指令数据
 * \details 接收队列容量为 QUEUE_MAX_SIZE - 1，满时丢弃新输入。
 *  \param  _data 指令数据
 */
extern void queue_push(qdata _data); /* 声明接口，将新字节追加到命令环形队列，保留一格用于判断满和空。 */

/*! 
 *  \brief  从指令队列中取出一条完整的指令
 *  \param  cmd 组帧输出缓冲区，分段接收期间须重复传入同一缓冲区
 *  \param  buf_len 指令接收缓存区大小
 *  \return 已提取帧的长度。未找到帧尾或启用 CRC 时校验失败均返回 0。
 *  \note  通过 EE 与 FF FC FF FF 分隔，不解析长度字段。超出 buf_len 的字节仅参与帧尾检测。
 */
extern qsize queue_find_cmd(qdata *cmd,qsize buf_len); /* 声明接口，从字节流匹配帧头和帧尾，取出一条完整命令。 */

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
