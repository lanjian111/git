#include "cmd_queue.h" /* 取得 HMI 字节队列和提取完整命令帧的接口。 */

#define CMD_HEAD 0XEE                                                  //帧头
#define CMD_TAIL 0XFFFCFFFF                                            //帧尾

typedef struct _QUEUE                                              /* 定义关联字段的结构类型，供相关接口复用。 */
{                                                                 
    qsize _head;                                                       // 下一个写入位置
    qsize _tail;                                                       // 下一个读取位置
    qdata _data[QUEUE_MAX_SIZE];                                       //队列数据缓存区
}QUEUE;                                                            /* 为上述定义命名为 QUEUE，便于在声明中引用。 */

static QUEUE que = {0,0,0};                                            //指令队列
static uint32 cmd_state = 0;                                           // 最近四个输入字节组成的滑动窗口
static qsize cmd_pos = 0;                                              // 已写入组帧缓冲区的字节数，跨调用保留

/*! 
*  \brief  清空指令数据
*/
void queue_reset() /* 同时清空环形队列和跨调用保留的帧匹配进度。 */
{
    que._head = que._tail = 0; /* 把读写下标重置到同一位置，使队列从空状态开始。 */
    cmd_pos = cmd_state = 0; /* 丢弃未完成帧的写入进度和帧尾匹配历史。 */
}
/*! 
* \brief  添加指令数据
* \details 留一格区分满和空。装满时静默丢弃新字节，保留已有数据。
*  \param  _data 指令数据
*/
void queue_push(qdata _data) /* 将新字节追加到命令环形队列，保留一格用于判断满和空。 */
{
    qsize pos = (que._head+1)%QUEUE_MAX_SIZE; /* 预计算绕回后的下一写入下标，用于和读下标比较。 */
    if(pos!=que._tail)                                                //非满状态
    {
        que._data[que._head] = _data; /* 把新字节写进当前队列槽，供之后的组帧读取。 */
        que._head = pos; /* 公布已经写好数据的队列新位置。 */
    }
}

//从队列中取一个数据
static void queue_pop(qdata* _data) /* 取出队列头一个未读字节，并释放对应槽位。 */
{
    if(que._tail!=que._head)                                          //非空状态
    {
        *_data = que._data[que._tail]; /* 把下一未读字节交给输出指针。 */
        que._tail = (que._tail+1)%QUEUE_MAX_SIZE; /* 跳到下一个未读槽并按队列大小回卷。 */
    }
}

//获取队列中有效数据个数
static qsize queue_size() /* 用读写下标的环形距离计算队列内已有的字节数。 */
{
    return ((que._head+QUEUE_MAX_SIZE-que._tail)%QUEUE_MAX_SIZE); /* 将 「((que._head+QUEUE_MAX_SIZE-que._tail)%QUEUE_MAX_SIZE)」 返回给调用者，供上层使用。 */
}
/*! 
*  \brief  从指令队列中取出一条完整的指令
*  \param  cmd 指令接收缓存区
*  \param  buf_len 指令接收缓存区大小
*  \return  指令长度，0表示队列中无完整指令
*/
qsize queue_find_cmd(qdata *buffer,qsize buf_len) /* 从字节流匹配帧头和帧尾，取出一条完整命令。 */
{
    qsize cmd_size = 0; /* 定义已识别完整帧的长度，将已识别完整帧的长度 更新为 「0」。 */
    qdata _data = 0; /* 定义数据，将数据 更新为 「0」。 */

    while(queue_size()>0) /* 在 「queue_size()>0」 成立期间重复处理循环体。 */
    {
        //取一个数据
        queue_pop(&_data); /* 取出队列头一个未读字节，并释放对应槽位。 */

        if(cmd_pos==0&&_data!=CMD_HEAD)                               //指令第一个字节必须是帧头，否则跳过
        {
            continue; /* 跳过当前项的其余处理，继续检查下一项。 */
        }
        /* 帧超出缓冲区时停止复制，仍继续匹配帧尾。 */
        if(cmd_pos<buf_len)                                           //防止缓冲区溢出
            buffer[cmd_pos++] = _data; /* 在容量检查通过后存入当前帧字节，同时记录已保存长度。 */

        cmd_state = ((cmd_state<<8)|_data);                           //拼接最后4个字节，组成一个32位整数

        /* 以帧尾签名分隔帧。超长帧在此会返回截断后的长度。 */
        if(cmd_state==CMD_TAIL) /* 最近四字节与帧尾签名相同时，判定本帧结束。 */
        {
            cmd_size = cmd_pos;                                       //指令字节长度
            cmd_state = 0;                                            //重新检测帧尾巴
            cmd_pos = 0;                                              //复位指令指针

#if(CRC16_ENABLE) /* 仅在「(CRC16_ENABLE)」 成立时编译下方代码。 */
            /* 跳过 1 字节帧头和 4 字节帧尾，校验区域末尾含 2 字节 CRC。 */
            if(!CheckCRC16(buffer+1,cmd_size-5))                      //CRC校验
                return 0; /* 以 0 返回本分支的结果，调用方据此处理当前状态。 */

            cmd_size -= 2;                                            // 返回长度扣除 CRC，但缓冲区字节不移动
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
            return cmd_size; /* 将 已识别完整帧的长度 返回给调用者，供上层使用。 */
        }
    }
    return 0;                                                         //没有形成完整的一帧
}
