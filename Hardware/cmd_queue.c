#include "cmd_queue.h"

#define CMD_HEAD 0XEE                                                  //帧头
#define CMD_TAIL 0XFFFCFFFF                                            //帧尾

typedef struct _QUEUE                                             
{                                                                 
    qsize _head;                                                       // 下一个写入位置
    qsize _tail;                                                       // 下一个读取位置
    qdata _data[QUEUE_MAX_SIZE];                                       //队列数据缓存区
}QUEUE;                                                           

static QUEUE que = {0,0,0};                                            //指令队列
static uint32 cmd_state = 0;                                           // 最近四个输入字节组成的滑动窗口
static qsize cmd_pos = 0;                                              // 已写入组帧缓冲区的字节数，跨调用保留

/*! 
*  \brief  清空指令数据
*/
void queue_reset()
{
    que._head = que._tail = 0;
    cmd_pos = cmd_state = 0;
}
/*! 
* \brief  添加指令数据
* \details 留一格区分满和空。装满时静默丢弃新字节，保留已有数据。
*  \param  _data 指令数据
*/
void queue_push(qdata _data)
{
    qsize pos = (que._head+1)%QUEUE_MAX_SIZE;
    if(pos!=que._tail)                                                //非满状态
    {
        que._data[que._head] = _data;
        que._head = pos;
    }
}

//从队列中取一个数据
static void queue_pop(qdata* _data)
{
    if(que._tail!=que._head)                                          //非空状态
    {
        *_data = que._data[que._tail];
        que._tail = (que._tail+1)%QUEUE_MAX_SIZE;
    }
}

//获取队列中有效数据个数
static qsize queue_size()
{
    return ((que._head+QUEUE_MAX_SIZE-que._tail)%QUEUE_MAX_SIZE);
}
/*! 
*  \brief  从指令队列中取出一条完整的指令
*  \param  cmd 指令接收缓存区
*  \param  buf_len 指令接收缓存区大小
*  \return  指令长度，0表示队列中无完整指令
*/
qsize queue_find_cmd(qdata *buffer,qsize buf_len)
{
    qsize cmd_size = 0;
    qdata _data = 0;

    while(queue_size()>0)
    {
        //取一个数据
        queue_pop(&_data);

        if(cmd_pos==0&&_data!=CMD_HEAD)                               //指令第一个字节必须是帧头，否则跳过
        {
            continue;
        }
        /* 帧超出缓冲区时停止复制，仍继续匹配帧尾。 */
        if(cmd_pos<buf_len)                                           //防止缓冲区溢出
            buffer[cmd_pos++] = _data;

        cmd_state = ((cmd_state<<8)|_data);                           //拼接最后4个字节，组成一个32位整数

        /* 以帧尾签名分隔帧。超长帧在此会返回截断后的长度。 */
        if(cmd_state==CMD_TAIL)
        {
            cmd_size = cmd_pos;                                       //指令字节长度
            cmd_state = 0;                                            //重新检测帧尾巴
            cmd_pos = 0;                                              //复位指令指针

#if(CRC16_ENABLE)
            /* 跳过 1 字节帧头和 4 字节帧尾，校验区域末尾含 2 字节 CRC。 */
            if(!CheckCRC16(buffer+1,cmd_size-5))                      //CRC校验
                return 0;

            cmd_size -= 2;                                            // 返回长度扣除 CRC，但缓冲区字节不移动
#endif
            return cmd_size;
        }
    }
    return 0;                                                         //没有形成完整的一帧
}
