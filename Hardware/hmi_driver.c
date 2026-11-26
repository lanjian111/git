/************************************版权申明********************************************
**                             广州大彩光电科技有限公司
**                             http://www.gz-dc.com
**-----------------------------------文件信息--------------------------------------------
** 文件名称:   hmi_driver.c
** 修改时间:   2018-05-18
** 文件说明:   用户MCU串口驱动函数库
** 技术支持：  Tel: 020-82186683  Email: hmi@gz-dc.com Web:www.gz-dc.com
--------------------------------------------------------------------------------------
----------------------------------------------------------------------------------------*/
#include "hmi_driver.h" /* 取得触摸屏命令封装和链路处理接口。 */
#include "cmd_queue.h" /* 取得 HMI 字节队列和提取完整命令帧的接口。 */
#include "cmd_process.h" /* 取得 HMI 帧结构及消息分发接口。 */
#include "flag.h" /* 取得采样、调试及流程启动标志的共享声明。 */
#include "elog.h" /* 取得日志级别、格式及输出入口的声明。 */
#include <stdio.h> /* 取得 snprintf 等受缓冲区容量约束的格式化接口。 */

#define LOG_TAG     "hmi" /* 指定日志标签 "hmi"，便于在串口输出中定位本模块。 */

/* 数字字段按大端写出。大部分厂商接口用 SendChar 逐字节发送并等待 DMA，
 * ReadRTC 单独组好整帧后一次提交，让主循环可以异步等回包。 */
#define TX_8(P1) SEND_DATA((P1)&0xFF)                    //发送单个字节
#define TX_8N(P,N) SendNU8((uint8 *)P,N)                 //发送N个字节
#define TX_16(P1) TX_8((P1)>>8);TX_8(P1)                 //发送16位整数
#define TX_16N(P,N) SendNU16((uint16 *)P,N)              //发送N个16位整数
#define TX_32(P1) TX_16((P1)>>16);TX_16((P1)&0xFFFF)     //发送32位整数

/* USART3 RX DMA 回调提供处理提示。短帧通常达不到回调触发条件，主循环还会轮询 DMA 写位置。 */
static volatile uint8 g_hmi_rx_pending = 0; /* 将DMA 回调留下的屏幕收数提示初始化为零，给模块一个确定的起始值。 */

static void HMI_RxCallback(uint8_t *data, uint16_t length) /* 在 DMA 回调中只置接收提示，解析留给主循环。 */
{
    // 回调里只做轻量标记，避免在中断上下文执行耗时解析逻辑。
    // data/length参数在当前实现中不直接使用，因为实际字节读取统一在HMI_LinkTask中完成。
    (void)data; /* 明确忽略 DMA 回调提供的数据指针，统一由主循环读取接收区。 */
    (void)length; /* 明确忽略回调长度，主循环按 DMA 未读数据量取字节。 */
    g_hmi_rx_pending = 1; /* 将DMA 回调留下的屏幕收数提示 更新为 「1」。 */
}

void HMI_LinkInit(void) /* 初始化 HMI 接收链路，清理历史接收状态并挂接回调。 */
{
    // 启动前先清空软件队列和DMA接收缓存，避免上电残留数据干扰协议状态机。
    queue_reset(); /* 同时清空环形队列和跨调用保留的帧匹配进度。 */
    USART2_DMA_ClearRxBuffer(); /* 重置屏幕串口的接收缓冲读写进度，丢弃旧数据。 */

    // 将 USART3 RX DMA 整圈回调绑定到 HMI 处理提示。
    USART2_DMA_SetRxCallback(HMI_RxCallback); /* 注册屏幕串口接收回调，在 DMA 通知时提示主循环抽取数据。 */

    // 清除待处理标志，保证初始化完成后从干净状态进入主循环。
    g_hmi_rx_pending = 0; /* 将DMA 回调留下的屏幕收数提示 更新为 「0」。 */
}

void HMI_LinkTask(void) /* 轮询屏幕 DMA 并取出完整帧，在主循环上下文分发。 */
{
    /* 主循环每次处理一帧时复用同一块缓冲区，使用静态存储可节省调用栈空间。 */
    static uint8 cmd_buf[CMD_MAX_SIZE]; /* 分配 「cmd_buf[CMD_MAX_SIZE]」 数组，作为当前操作的数据缓冲区。 */
    qsize cmd_len = 0; /* 定义命令 长度，将命令 长度清零，使本轮从初始计数开始。 */

    // 没有回调触发且DMA当前无可读数据时，立即返回降低CPU占用。
    if ((g_hmi_rx_pending == 0U) && (USART2_DMA_Available() == 0U)) /* 没有回调触发且DMA当前无可读数据时，立即返回降低CPU占用。 */
    {
        return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */
    }

    // 本轮开始处理，先清标志；若处理期间又收到新数据，回调会再次置位。
    g_hmi_rx_pending = 0; /* 将DMA 回调留下的屏幕收数提示 更新为 「0」。 */

    /* 先搬入软件队列，再寻找帧头和帧尾。队列容量为 127 字节，单次输入
     * 过多时 queue_push 会丢弃后续字节。组帧进度和 cmd_buf 均保留到下一轮。 */
    while (USART2_DMA_Available() > 0U) /* 在 「USART2_DMA_Available() > 0U」 成立期间重复处理循环体。 */
    {
        queue_push((qdata)USART2_DMA_ReadByte()); /* 把屏幕 DMA 中的下一个未读字节转入软件队列，供帧匹配使用。 */
    }

    // 依次分发取到的帧。ProcessMessage 可就地改写 cmd_buf，因此先记录原始字节。
    while ((cmd_len = queue_find_cmd(cmd_buf, CMD_MAX_SIZE)) > 0U) /* 依次分发取到的帧。ProcessMessage 可就地改写 cmd_buf，因此先记录原始字节。 */
    {
        // 调试标志开启时准备十六进制帧文本，实际输出还受 EasyLogger 级别与格式配置约束。
        if (FLAG_DEBUG_MODE) /* 调试标志开启时准备十六进制帧文本，实际输出还受 EasyLogger 级别与格式配置约束。 */
        {
            char hex_buf[CMD_MAX_SIZE * 3 + 20]; /* 分配 「hex_buf[CMD_MAX_SIZE * 3 + 20]」 数组，作为当前操作的数据缓冲区。 */
            int pos = snprintf(hex_buf, sizeof(hex_buf), "HMI_RX[%u]:", cmd_len); /* 定义位置，将位置 更新为 「snprintf(hex_buf, sizeof(hex_buf), "HMI_RX[%u]:", cmd_len)」。 */
            for (qsize i = 0; i < cmd_len && pos < (int)sizeof(hex_buf) - 4; i++) /* 从 「qsize i = 0」 开始，在 「i < cmd_len && pos < (int)sizeof(hex_buf) - 4」 成立时逐项处理，每轮执行 「i++」。 */
            {
                pos += snprintf(hex_buf + pos, sizeof(hex_buf) - pos, " %02X", cmd_buf[i]); /* 将位置 累加 「snprintf(hex_buf + pos, sizeof(hex_buf) - pos, " %02X", cmd_buf[i])」。 */
            }
            log_d("%s", hex_buf); /* 以调试级别记录 「"%s", hex_buf」，便于从日志跟踪当前分支。 */
        }
        ProcessMessage((PCTRL_MSG)cmd_buf, cmd_len); /* 将当前完整帧交给屏幕消息分发器，传入实际提取出的长度。 */
    }
}

#if(CRC16_ENABLE) /* 仅在「(CRC16_ENABLE)」 成立时编译下方代码。 */

static uint16 _crc16 = 0xffff; /* 定义_crc16，将_crc16 更新为 「0xffff」。 */
/*!
*  \brief 添加CRC16校验
*  \param buffer 待校验的数据
*  \param n 本次参与计算的字节数
*  \param pcrc CRC 累积值，开始新帧时初始化为 0xFFFF
*/
static void AddCRC16(uint8 *buffer,uint16 n,uint16 *pcrc) /* 将指定字节纳入 CRC-16 累积值。 */
{
    uint16 i,j,carry_flag,a; /* 声明 「i,j,carry_flag,a」，用于本函数的中间结果和循环控制。 */

    for (i=0; i<n; i++) /* 从 「i=0」 开始，在 「i<n」 成立时逐项处理，每轮执行 「i++」。 */
    {
        *pcrc=*pcrc^buffer[i]; /* 将pcrc 更新为 「*pcrc^buffer[i]」。 */
        for (j=0; j<8; j++) /* 从 「j=0」 开始，在 「j<8」 成立时逐项处理，每轮执行 「j++」。 */
        {
            a=*pcrc; /* 将a 更新为 「*pcrc」。 */
            carry_flag=a&0x0001; /* 将carry 标志 更新为 「a&0x0001」。 */
            *pcrc=*pcrc>>1; /* 将pcrc 更新为 「*pcrc>>1」。 */
            if (carry_flag==1) /* 当 「carry_flag==1」 成立时处理下方分支。 */
                *pcrc=*pcrc^0xa001; /* 将pcrc 更新为 「*pcrc^0xa001」。 */
        }
    }
}
/*!
*  \brief  检查数据是否符合CRC16校验
*  \param buffer 待校验的数据，末尾存储CRC16
*  \param n 数据长度，包含CRC16
*  \return 校验通过返回1，否则返回0
*/
uint16 CheckCRC16(uint8 *buffer,uint16 n) /* 比较帧内的 CRC 与按有效字节重算的 CRC。 */
{
    uint16 crc0 = 0x0; /* 定义crc0，将crc0 更新为 「0x0」。 */
    uint16 crc1 = 0xffff; /* 定义crc1，将crc1 更新为 「0xffff」。 */

    if(n>=2) /* 当 「n>=2」 成立时处理下方分支。 */
    {
        crc0 = ((buffer[n-2]<<8)|buffer[n-1]); /* 将crc0 更新为 「((buffer[n-2]<<8)|buffer[n-1])」。 */
        AddCRC16(buffer,n-2,&crc1); /* 将指定字节纳入 CRC-16 累积值。 */
    }

    return (crc0==crc1); /* 将 「(crc0==crc1)」 返回给调用者，供上层使用。 */
}
/*!
*  \brief  发送一个字节
*  \param  c
*/
void SEND_DATA(uint8 c) /* 将单字节送入屏幕发送链路。 */
{
    AddCRC16(&c,1,&_crc16); /* 将指定字节纳入 CRC-16 累积值。 */
    SendChar(c); /* 将一个字节交给屏幕串口发送。 */
}
/*!
*  \brief  帧头
*/
void BEGIN_CMD() /* 写入触摸屏帧头，标记命令起始。 */
{
    TX_8(0XEE); /* 按 8 位发送帧头 EE，供接收端定位命令开始。 */
    _crc16 = 0XFFFF;                      //开始计算CRC16
}
/*!
*  \brief  帧尾
*/
void END_CMD() /* 写入触摸屏帧尾，标记命令结束。 */
{
    uint16 crc16 = _crc16; /* 定义crc16，将crc16 更新为 「_crc16」。 */
    TX_16(crc16);                         //发送CRC16
    TX_32(0XFFFCFFFF); /* 按 32 位发送帧尾 FF FC FF FF，供接收端界定命令结束。 */
}

#else /* 前面的编译条件不成立时，改用下方实现或配置。 */ //NO CRC16

#define SEND_DATA(P) SendChar(P)          //发送一个字节
#define BEGIN_CMD() TX_8(0XEE)            //帧头
#define END_CMD() TX_32(0XFFFCFFFF)       //帧尾

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
/*!
*  \brief  厂商接口保留的空循环延时，占用 CPU，耗时依赖编译优化和主频。
*  \param  n 外层循环次数
*/
void DelayMS(unsigned int n) /* 厂商接口保留的空循环延时，占用 CPU，耗时依赖编译优化和主频。 */
{
    int i,j; /* 声明 「i,j」，用于本函数的中间结果和循环控制。 */
    for(i = n;i>0;i--) /* 从 「i = n」 开始，在 「i>0」 成立时逐项处理，每轮执行 「i--」。 */
        for(j=1000;j>0;j--) ; /* 从 「j=1000」 开始，在 「j>0」 成立时逐项处理，每轮执行 「j--」。 */
}
/*!
*  \brief  逐字节发送字符串，遇到 NUL 结束。
*  \param  str 指向以 NUL 结尾的字节串
*/
void SendStrings(uchar *str) /* 逐字节发送字符串，遇到 NUL 结束。 */
{
    while(*str) /* 在 「*str」 成立期间重复处理循环体。 */
    {
        TX_8(*str); /* 按 8 位写出字段 「*str」，让屏幕按约定顺序解释后续命令。 */
        str++; /* 将str加一，推进处理进度。 */
    }
}
/*!
*  \brief  按指定长度逐字节发送，支持数据中含 0x00。
*  \param  pData 数据首地址
*  \param  nDataLen 字节数
*/
void SendNU8(uint8 *pData,uint16 nDataLen) /* 发送给定长度的字节数组，保留其中可能出现的 0 字节。 */
{
    uint16 i = 0; /* 定义i，将i 更新为 「0」。 */
    for (;i<nDataLen;++i) /* 沿用当前下标，在 「i<nDataLen」 成立时逐项处理，每轮执行 「++i」。 */
    {
        TX_8(pData[i]); /* 按 8 位写出字段 「pData[i]」，让屏幕按约定顺序解释后续命令。 */
    }
}
/*!
*  \brief  发送数组中的 16 位整数，每个元素高字节在前。
*  \param  pData 数组首地址
*  \param  nDataLen 元素数
*/
void SendNU16(uint16 *pData,uint16 nDataLen) /* 按高字节在前发送指定数量的 16 位数。 */
{
    uint16 i = 0; /* 定义i，将i 更新为 「0」。 */
    for (;i<nDataLen;++i) /* 沿用当前下标，在 「i<nDataLen」 成立时逐项处理，每轮执行 「++i」。 */
    {
        TX_16(pData[i]); /* 按 16 位写出字段 「pData[i]」，多字节高位先发以符合屏幕协议。 */
    }
}
/*!
*  \brief  发送握手命令
*/
void SetHandShake() /* 发送握手命令。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x04); /* 写入 0x04，作为对应发送握手命令的协议字节。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

/*!
*  \brief  设置前景色
*  \param  color 前景色
*/
void SetFcolor(uint16 color) /* 设置前景色。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x41); /* 写入 0x41，作为对应设置前景色的协议字节。 */
    TX_16(color); /* 按 16 位发送颜色值。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief  设置背景色
*  \param  color 背景色
*/
void SetBcolor(uint16 color) /* 设置背景色。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x42); /* 写入 0x42，作为对应设置背景色的协议字节。 */
    TX_16(color); /* 按 16 位发送颜色值。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief  发送取色命令 0xA3。
*  \param  mode 原样发送的取色模式
*  \param  x 取色位置 X 坐标
*  \param  y 取色位置 Y 坐标
*/
void ColorPicker(uint8 mode, uint16 x,uint16 y) /* 发送取色命令 0xA3。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xA3); /* 写入 0xA3，作为对应发送取色命令 0xA3的协议字节。 */
    TX_8(mode); /* 按 8 位发送工作模式。 */
    TX_16(x); /* 按 16 位发送横坐标。 */
    TX_16(y); /* 按 16 位发送纵坐标。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief  清除画面
*/
void GUI_CleanScreen() /* 清除画面。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x01); /* 写入 0x01，作为对应清除画面的协议字节。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief  设置文字间隔
*  \param  x_w 横向间隔
*  \param  y_w 纵向间隔
*/
void SetTextSpace(uint8 x_w, uint8 y_w) /* 设置文字间隔。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x43); /* 写入 0x43，作为对应设置文字间隔的协议字节。 */
    TX_8(x_w); /* 按 8 位写出字段 「x_w」，让屏幕按约定顺序解释后续命令。 */
    TX_8(y_w); /* 按 8 位写出字段 「y_w」，让屏幕按约定顺序解释后续命令。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief  设置文字显示限制
*  \param  enable 是否启用限制
*  \param  width 宽度
*  \param  height 高度
*/
void SetFont_Region(uint8 enable,uint16 width,uint16 height) /* 设置文字显示限制。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x45); /* 写入 0x45，作为对应设置文字显示限制的协议字节。 */
    TX_8(enable); /* 按 8 位发送启用标记。 */
    TX_16(width); /* 按 16 位写出字段 「width」，多字节高位先发以符合屏幕协议。 */
    TX_16(height); /* 按 16 位写出字段 「height」，多字节高位先发以符合屏幕协议。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief  设置过滤色
*  \param  fillcolor_dwon 颜色下界
*  \param  fillcolor_up 颜色上界
*/
void SetFilterColor(uint16 fillcolor_dwon, uint16 fillcolor_up) /* 设置过滤色。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x44); /* 写入 0x44，作为对应设置过滤色的协议字节。 */
    TX_16(fillcolor_dwon); /* 按 16 位写出字段 「fillcolor_dwon」，多字节高位先发以符合屏幕协议。 */
    TX_16(fillcolor_up); /* 按 16 位写出字段 「fillcolor_up」，多字节高位先发以符合屏幕协议。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

/*!
*  \brief  在指定坐标绘制文本，内容发送到首个 NUL 为止。
*  \param  x 位置X坐标
*  \param  y 位置Y坐标
*  \param  back 背景绘制参数，按一个字节发送
*  \param  font 字体
*  \param  strings 字符串内容
*/
void DisText(uint16 x, uint16 y,uint8 back,uint8 font,uchar *strings ) /* 在指定坐标绘制文本，内容发送到首个 NUL 为止。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x20); /* 写入 0x20，作为对应在指定坐标绘制文本，内容发送到首个 NUL 为止的协议字节。 */
    TX_16(x); /* 按 16 位发送横坐标。 */
    TX_16(y); /* 按 16 位发送纵坐标。 */
    TX_8(back); /* 按 8 位写出字段 「back」，让屏幕按约定顺序解释后续命令。 */
    TX_8(font); /* 按 8 位发送字体编号。 */
    SendStrings(strings); /* 逐字节发送字符串，遇到 NUL 结束。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief    显示光标
*  \param  enable 是否显示
*  \param  x 位置X坐标
*  \param  y 位置Y坐标
*  \param  width 宽度
*  \param  height 高度
*/
void DisCursor(uint8 enable,uint16 x, uint16 y,uint8 width,uint8 height ) /* 显示光标。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x21); /* 写入 0x21，作为对应显示光标的协议字节。 */
    TX_8(enable); /* 按 8 位发送启用标记。 */
    TX_16(x); /* 按 16 位发送横坐标。 */
    TX_16(y); /* 按 16 位发送纵坐标。 */
    TX_8(width); /* 按 8 位写出字段 「width」，让屏幕按约定顺序解释后续命令。 */
    TX_8(height); /* 按 8 位写出字段 「height」，让屏幕按约定顺序解释后续命令。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      显示全屏图片
*  \param  image_id 图片索引
*  \param  masken 是否启用透明掩码
*/
void DisFull_Image(uint16 image_id,uint8 masken) /* 显示全屏图片。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x31); /* 写入 0x31，作为对应显示全屏图片的协议字节。 */
    TX_16(image_id); /* 按 16 位发送图片资源编号。 */
    TX_8(masken); /* 按 8 位发送透明色遮罩开关。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      指定位置显示图片
*  \param  x 位置X坐标
*  \param  y 位置Y坐标
*  \param  image_id 图片索引
*  \param  masken 是否启用透明掩码
*/
void DisArea_Image(uint16 x,uint16 y,uint16 image_id,uint8 masken) /* 指定位置显示图片。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x32); /* 写入 0x32，作为对应指定位置显示图片的协议字节。 */
    TX_16(x); /* 按 16 位发送横坐标。 */
    TX_16(y); /* 按 16 位发送纵坐标。 */
    TX_16(image_id); /* 按 16 位发送图片资源编号。 */
    TX_8(masken); /* 按 8 位发送透明色遮罩开关。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      显示裁剪图片
*  \param  x 位置X坐标
*  \param  y 位置Y坐标
*  \param  image_id 图片索引
*  \param  image_x 图片裁剪位置X坐标
*  \param  image_y 图片裁剪位置Y坐标
*  \param  image_l 图片裁剪长度
*  \param  image_w 图片裁剪高度
*  \param  masken 是否启用透明掩码
*/
void DisCut_Image(uint16 x,uint16 y,uint16 image_id,uint16 image_x,uint16 image_y,uint16 image_l, uint16 image_w,uint8 masken) /* 显示裁剪图片。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x33); /* 写入 0x33，作为对应显示裁剪图片的协议字节。 */
    TX_16(x); /* 按 16 位发送横坐标。 */
    TX_16(y); /* 按 16 位发送纵坐标。 */
    TX_16(image_id); /* 按 16 位发送图片资源编号。 */
    TX_16(image_x); /* 按 16 位写出字段 「image_x」，多字节高位先发以符合屏幕协议。 */
    TX_16(image_y); /* 按 16 位写出字段 「image_y」，多字节高位先发以符合屏幕协议。 */
    TX_16(image_l); /* 按 16 位写出字段 「image_l」，多字节高位先发以符合屏幕协议。 */
    TX_16(image_w); /* 按 16 位写出字段 「image_w」，多字节高位先发以符合屏幕协议。 */
    TX_8(masken); /* 按 8 位发送透明色遮罩开关。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      显示GIF动画
*  \param  x 位置X坐标
*  \param  y 位置Y坐标
*  \param  flashimage_id 图片索引
*  \param  enable 是否显示
*  \param  playnum 播放次数
*/
void DisFlashImage(uint16 x,uint16 y,uint16 flashimage_id,uint8 enable,uint8 playnum) /* 显示GIF动画。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x80); /* 写入 0x80，作为对应显示GIF动画的协议字节。 */
    TX_16(x); /* 按 16 位发送横坐标。 */
    TX_16(y); /* 按 16 位发送纵坐标。 */
    TX_16(flashimage_id); /* 按 16 位发送动画图片资源编号。 */
    TX_8(enable); /* 按 8 位发送启用标记。 */
    TX_8(playnum); /* 按 8 位写出字段 「playnum」，让屏幕按约定顺序解释后续命令。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      画点
*  \param  x 位置X坐标
*  \param  y 位置Y坐标
*/
void GUI_Dot(uint16 x,uint16 y) /* 画点。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x50); /* 写入 0x50，作为对应画点的协议字节。 */
    TX_16(x); /* 按 16 位发送横坐标。 */
    TX_16(y); /* 按 16 位发送纵坐标。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      画线
*  \param  x0 起始位置X坐标
*  \param  y0 起始位置Y坐标
*  \param  x1 结束位置X坐标
*  \param  y1 结束位置Y坐标
*/
void GUI_Line(uint16 x0, uint16 y0, uint16 x1, uint16 y1) /* 画线。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x51); /* 写入 0x51，作为对应画线的协议字节。 */
    TX_16(x0); /* 按 16 位发送起点横坐标。 */
    TX_16(y0); /* 按 16 位发送起点纵坐标。 */
    TX_16(x1); /* 按 16 位发送终点横坐标。 */
    TX_16(y1); /* 按 16 位发送终点纵坐标。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

/*!
*  \brief      画折线
*  \param  mode 模式
*  \param  dot 数据点
*  \param  dot_cnt 点数
*/
void GUI_ConDots(uint8 mode,uint16 *dot,uint16 dot_cnt) /* 画折线。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x63); /* 写入 0x63，作为对应画折线的协议字节。 */
    TX_8(mode); /* 按 8 位发送工作模式。 */
    TX_16N(dot,dot_cnt*2); /* 按 16 位元素依次写出「dot,dot_cnt*2」 指定的数据及数量。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

/*!
*  \brief   x坐标等距使用前景色连线
*  \param  x 横坐标
*  \param  x_space 距离
*  \param  dot_y  一组纵轴坐标
*  \param  dot_cnt  纵坐标个数
*/
void GUI_ConSpaceDots(uint16 x,uint16 x_space,uint16 *dot_y,uint16 dot_cnt) /* x坐标等距使用前景色连线。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x59); /* 写入 0x59，作为对应x坐标等距使用前景色连线的协议字节。 */
    TX_16(x); /* 按 16 位发送横坐标。 */
    TX_16(x_space); /* 按 16 位写出字段 「x_space」，多字节高位先发以符合屏幕协议。 */
    TX_16N(dot_y,dot_cnt); /* 按 16 位元素依次写出「dot_y,dot_cnt」 指定的数据及数量。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief   按照坐标偏移量用前景色连线
*  \param  x 横坐标
*  \param  y 纵距离
*  \param  dot_offset  偏移量
*  \param  dot_cnt  偏移量个数
*/
void GUI_FcolorConOffsetDots(uint16 x,uint16 y,uint16 *dot_offset,uint16 dot_cnt) /* 按照坐标偏移量用前景色连线。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x75); /* 写入 0x75，作为对应按照坐标偏移量用前景色连线的协议字节。 */
    TX_16(x); /* 按 16 位发送横坐标。 */
    TX_16(y); /* 按 16 位发送纵坐标。 */
    TX_16N(dot_offset,dot_cnt); /* 按 16 位元素依次写出「dot_offset,dot_cnt」 指定的数据及数量。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief   按照坐标偏移量用背景色连线
*  \param  x 横坐标
*  \param  y 纵距离
*  \param  dot_offset  偏移量
*  \param  dot_cnt  偏移量个数
*/
void GUI_BcolorConOffsetDots(uint16 x,uint16 y,uint8 *dot_offset,uint16 dot_cnt) /* 按照坐标偏移量用背景色连线。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x76); /* 写入 0x76，作为对应按照坐标偏移量用背景色连线的协议字节。 */
    TX_16(x); /* 按 16 位发送横坐标。 */
    TX_16(y); /* 按 16 位发送纵坐标。 */
    TX_16N(dot_offset,dot_cnt); /* 按 16 位元素依次写出「dot_offset,dot_cnt」 指定的数据及数量。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief  自动调节背光亮度
*  \param  enable 使能
*  \param  bl_off_level 待机亮度
*  \param  bl_on_level  激活亮度
*  \param  bl_on_time  偏移量个数
*/
void SetPowerSaving(uint8 enable, uint8 bl_off_level, uint8 bl_on_level, uint8  bl_on_time) /* 自动调节背光亮度。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x77); /* 写入 0x77，作为对应自动调节背光亮度的协议字节。 */
    TX_8(enable); /* 按 8 位发送启用标记。 */
    TX_8(bl_off_level); /* 按 8 位写出字段 「bl_off_level」，让屏幕按约定顺序解释后续命令。 */
    TX_8(bl_on_level); /* 按 8 位写出字段 「bl_on_level」，让屏幕按约定顺序解释后续命令。 */
    TX_8(bl_on_time); /* 按 8 位写出字段 「bl_on_time」，让屏幕按约定顺序解释后续命令。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief  将制定的多个坐标点用前景色连接起来
*  \param  dot  坐标点
*  \param  dot_cnt  偏移量个数
*/
void GUI_FcolorConDots(uint16 *dot,uint16 dot_cnt) /* 将制定的多个坐标点用前景色连接起来。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x68); /* 写入 0x68，作为对应将制定的多个坐标点用前景色连接起来的协议字节。 */
    TX_16N(dot,dot_cnt*2); /* 按 16 位元素依次写出「dot,dot_cnt*2」 指定的数据及数量。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief  将制定的多个坐标点用背景色连接起来
*  \param  dot  坐标点
*  \param  dot_cnt  偏移量个数
*/
void GUI_BcolorConDots(uint16 *dot,uint16 dot_cnt) /* 将制定的多个坐标点用背景色连接起来。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x69); /* 写入 0x69，作为对应将制定的多个坐标点用背景色连接起来的协议字节。 */
    TX_16N(dot,dot_cnt*2); /* 按 16 位元素依次写出「dot,dot_cnt*2」 指定的数据及数量。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     画空心圆
*  \param  x 圆心 X 坐标
*  \param  y 圆心 Y 坐标
*  \param  r 半径
*/
void GUI_Circle(uint16 x, uint16 y, uint16 r) /* 画空心圆。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x52); /* 写入 0x52，作为对应画空心圆的协议字节。 */
    TX_16(x); /* 按 16 位发送横坐标。 */
    TX_16(y); /* 按 16 位发送纵坐标。 */
    TX_16(r); /* 按 16 位发送半径。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      画实心圆
*  \param  x 圆心 X 坐标
*  \param  y 圆心 Y 坐标
*  \param  r 半径
*/
void GUI_CircleFill(uint16 x, uint16 y, uint16 r) /* 画实心圆。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x53); /* 写入 0x53，作为对应画实心圆的协议字节。 */
    TX_16(x); /* 按 16 位发送横坐标。 */
    TX_16(y); /* 按 16 位发送纵坐标。 */
    TX_16(r); /* 按 16 位发送半径。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      画弧线
*  \param  x 圆心 X 坐标
*  \param  y 圆心 Y 坐标
*  \param  r 半径
*  \param  sa 起始角度
*  \param  ea 终止角度
*/
void GUI_Arc(uint16 x,uint16 y, uint16 r,uint16 sa, uint16 ea) /* 画弧线。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x67); /* 写入 0x67，作为对应画弧线的协议字节。 */
    TX_16(x); /* 按 16 位发送横坐标。 */
    TX_16(y); /* 按 16 位发送纵坐标。 */
    TX_16(r); /* 按 16 位发送半径。 */
    TX_16(sa); /* 按 16 位发送起始角。 */
    TX_16(ea); /* 按 16 位发送结束角。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      画空心矩形
*  \param  x0 起始位置X坐标
*  \param  y0 起始位置Y坐标
*  \param  x1 结束位置X坐标
*  \param  y1 结束位置Y坐标
*/
void GUI_Rectangle(uint16 x0, uint16 y0, uint16 x1,uint16 y1 ) /* 画空心矩形。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x54); /* 写入 0x54，作为对应画空心矩形的协议字节。 */
    TX_16(x0); /* 按 16 位发送起点横坐标。 */
    TX_16(y0); /* 按 16 位发送起点纵坐标。 */
    TX_16(x1); /* 按 16 位发送终点横坐标。 */
    TX_16(y1); /* 按 16 位发送终点纵坐标。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      画实心矩形
*  \param  x0 起始位置X坐标
*  \param  y0 起始位置Y坐标
*  \param  x1 结束位置X坐标
*  \param  y1 结束位置Y坐标
*/
void GUI_RectangleFill(uint16 x0, uint16 y0, uint16 x1,uint16 y1 ) /* 画实心矩形。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x55); /* 写入 0x55，作为对应画实心矩形的协议字节。 */
    TX_16(x0); /* 按 16 位发送起点横坐标。 */
    TX_16(y0); /* 按 16 位发送起点纵坐标。 */
    TX_16(x1); /* 按 16 位发送终点横坐标。 */
    TX_16(y1); /* 按 16 位发送终点纵坐标。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      画空心椭圆
*  \param  x0 起始位置X坐标
*  \param  y0 起始位置Y坐标
*  \param  x1 结束位置X坐标
*  \param  y1 结束位置Y坐标
*/
void GUI_Ellipse(uint16 x0, uint16 y0, uint16 x1,uint16 y1 ) /* 画空心椭圆。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x56); /* 写入 0x56，作为对应画空心椭圆的协议字节。 */
    TX_16(x0); /* 按 16 位发送起点横坐标。 */
    TX_16(y0); /* 按 16 位发送起点纵坐标。 */
    TX_16(x1); /* 按 16 位发送终点横坐标。 */
    TX_16(y1); /* 按 16 位发送终点纵坐标。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      画实心椭圆
*  \param  x0 起始位置X坐标
*  \param  y0 起始位置Y坐标
*  \param  x1 结束位置X坐标
*  \param  y1 结束位置Y坐标
*/
void GUI_EllipseFill(uint16 x0, uint16 y0, uint16 x1,uint16 y1 ) /* 画实心椭圆。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x57); /* 写入 0x57，作为对应画实心椭圆的协议字节。 */
    TX_16(x0); /* 按 16 位发送起点横坐标。 */
    TX_16(y0); /* 按 16 位发送起点纵坐标。 */
    TX_16(x1); /* 按 16 位发送终点横坐标。 */
    TX_16(y1); /* 按 16 位发送终点纵坐标。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      发送屏幕背光亮度设置指令 0x60。
*  \param  light_level 单字节亮度参数，原样发送
*/
void SetBackLight(uint8 light_level) /* 发送屏幕背光亮度设置指令 0x60。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x60); /* 写入 0x60，作为对应发送屏幕背光亮度设置指令 0x60的协议字节。 */
    TX_8(light_level); /* 按 8 位发送背光亮度。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

/*!
*  \brief   蜂鸣器设置
*  \param  time 发送给屏幕的单字节蜂鸣持续时间参数
*/
void SetBuzzer(uint8 time) /* 蜂鸣器设置。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x61); /* 写入 0x61，作为对应蜂鸣器设置的协议字节。 */
    TX_8(time); /* 按 8 位发送蜂鸣器持续时间。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

/* 发送命令 0x65，参数为矩形的两组角坐标，按大端排列。 */
void GUI_AreaInycolor(uint16 x0, uint16 y0, uint16 x1,uint16 y1 ) /* 发送区域反色命令，用两组角坐标指定屏幕上的矩形范围。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x65); /* 写入 0x65，作为对应发送区域反色命令，用两组角坐标指定屏幕上的矩形范围的协议字节。 */
    TX_16(x0); /* 按 16 位发送起点横坐标。 */
    TX_16(y0); /* 按 16 位发送起点纵坐标。 */
    TX_16(x1); /* 按 16 位发送终点横坐标。 */
    TX_16(y1); /* 按 16 位发送终点纵坐标。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief   触摸屏设置
*  \param enbale 触摸使能（参数名沿用兼容接口）
*  \param beep_on 触摸蜂鸣器
*  \param work_mode 触摸工作模式：0按下就上传，1松开才上传，2不断上传坐标值，3按下和松开均上传数据
*  \param press_calibration 连续点击触摸屏20下校准触摸屏：0禁用，1启用
*/
void SetTouchPaneOption(uint8 enbale,uint8 beep_on,uint8 work_mode,uint8 press_calibration) /* 触摸屏设置。 */
{
    uint8 options = 0; /* 定义options，将options 更新为 「0」。 */

    if(enbale) /* 当 「enbale」 成立时处理下方分支。 */
        options |= 0x01; /* 将options 按位置位 「0x01」。 */
    if(beep_on) /* 当 「beep_on」 成立时处理下方分支。 */
        options |= 0x02; /* 将options 按位置位 「0x02」。 */
    if(work_mode) /* 当 「work_mode」 成立时处理下方分支。 */
        options |= (work_mode<<2); /* 将options 按位置位 「(work_mode<<2)」。 */
    if(press_calibration) /* 当 「press_calibration」 成立时处理下方分支。 */
        options |= (press_calibration<<5); /* 将options 按位置位 「(press_calibration<<5)」。 */

    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x70); /* 写入 0x70，作为对应触摸屏设置的协议字节。 */
    TX_8(options); /* 按 8 位写出字段 「options」，让屏幕按约定顺序解释后续命令。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief   校准触摸屏
*/
void CalibrateTouchPane() /* 校准触摸屏。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x72); /* 写入 0x72，作为对应校准触摸屏的协议字节。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief  触摸屏测试
*/
void TestTouchPane() /* 触摸屏测试。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x73); /* 写入 0x73，作为对应触摸屏测试的协议字节。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

/*!
*  \brief  锁定设备配置，锁定之后需要解锁，才能修改波特率、触摸屏、蜂鸣器工作方式
*/
void UnlockDeviceConfig(void) /* 锁定设备配置，锁定之后需要解锁，才能修改波特率、触摸屏、蜂鸣器工作方式。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x09); /* 写入 0x09，作为对应锁定设备配置，锁定之后需要解锁，才能修改波特率、触摸屏、蜂鸣器工作方式的协议字节。 */
    TX_8(0xDE); /* 写入 0xDE，作为对应锁定设备配置，锁定之后需要解锁，才能修改波特率、触摸屏、蜂鸣器工作方式的协议字节。 */
    TX_8(0xED); /* 写入 0xED，作为对应锁定设备配置，锁定之后需要解锁，才能修改波特率、触摸屏、蜂鸣器工作方式的协议字节。 */
    TX_8(0x13); /* 写入 0x13，作为对应锁定设备配置，锁定之后需要解锁，才能修改波特率、触摸屏、蜂鸣器工作方式的协议字节。 */
    TX_8(0x31); /* 写入 0x31，作为对应锁定设备配置，锁定之后需要解锁，才能修改波特率、触摸屏、蜂鸣器工作方式的协议字节。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

/*!
*  \brief  解锁设备配置
*/
void LockDeviceConfig(void) /* 解锁设备配置。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x08); /* 写入 0x08，作为对应解锁设备配置的协议字节。 */
    TX_8(0xA5); /* 写入 0xA5，作为对应解锁设备配置的协议字节。 */
    TX_8(0x5A); /* 写入 0x5A，作为对应解锁设备配置的协议字节。 */
    TX_8(0x5F); /* 写入 0x5F，作为对应解锁设备配置的协议字节。 */
    TX_8(0xF5); /* 写入 0xF5，作为对应解锁设备配置的协议字节。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief    修改串口屏的波特率
*  \details  波特率选项范围[0~14]，对应实际波特率
{1200,2400,4800,9600,19200,38400,57600,115200,1000000,2000000,218750,437500,875000,921800,2500000}
*  \param  option 波特率选项
*/
void SetCommBps(uint8 option) /* 修改串口屏的波特率。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xA0); /* 写入 0xA0，作为对应修改串口屏的波特率的协议字节。 */
    TX_8(option); /* 按 8 位写出字段 「option」，让屏幕按约定顺序解释后续命令。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      设置当前写入图层
*  \details  一般用于实现双缓存效果(绘图时避免闪烁)：
*  \details  uint8 layer = 0;
*  \details  WriteLayer(layer);   设置写入层
*  \details  ClearLayer(layer);   使图层变透明
*  \details  添加一系列绘图指令
*  \details  DisText(100,100,0,4,"hello hmi!!!");
*  \details  DisplyLayer(layer);  切换显示层
*  \details  layer = (layer+1)%2; 双缓存切换
*  \see DisplyLayer
*  \see ClearLayer
*  \param  layer 图层编号
*/
void WriteLayer(uint8 layer) /* 设置当前写入图层。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xA1); /* 写入 0xA1，作为对应设置当前写入图层的协议字节。 */
    TX_8(layer); /* 按 8 位写出字段 「layer」，让屏幕按约定顺序解释后续命令。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      设置当前显示图层
*  \param  layer 图层编号
*/
void DisplyLayer(uint8 layer) /* 设置当前显示图层。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xA2); /* 写入 0xA2，作为对应设置当前显示图层的协议字节。 */
    TX_8(layer); /* 按 8 位写出字段 「layer」，让屏幕按约定顺序解释后续命令。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      拷贝图层
*  \param  src_layer 原始图层
*  \param  dest_layer 目标图层
*/
void CopyLayer(uint8 src_layer,uint8 dest_layer) /* 拷贝图层。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xA4); /* 写入 0xA4，作为对应拷贝图层的协议字节。 */
    TX_8(src_layer); /* 按 8 位写出字段 「src_layer」，让屏幕按约定顺序解释后续命令。 */
    TX_8(dest_layer); /* 按 8 位写出字段 「dest_layer」，让屏幕按约定顺序解释后续命令。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      清除图层，使图层变成透明
*  \param  layer 图层编号
*/
void ClearLayer(uint8 layer) /* 清除图层，使图层变成透明。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x05); /* 写入 0x05，作为对应清除图层，使图层变成透明的协议字节。 */
    TX_8(layer); /* 按 8 位写出字段 「layer」，让屏幕按约定顺序解释后续命令。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

void GUI_DispRTC(uint8 enable,uint8 mode,uint8 font,uint16 color,uint16 x,uint16 y) /* GUI Disp 实时时钟。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x85); /* 写入 0x85，作为对应GUI Disp 实时时钟的协议字节。 */
    TX_8(enable); /* 按 8 位发送启用标记。 */
    TX_8(mode); /* 按 8 位发送工作模式。 */
    TX_8(font); /* 按 8 位发送字体编号。 */
    TX_16(color); /* 按 16 位发送颜色值。 */
    TX_16(x); /* 按 16 位发送横坐标。 */
    TX_16(y); /* 按 16 位发送纵坐标。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief  写数据到串口屏用户存储区
*  \param  startAddress 起始地址
*  \param  length 字节数
*  \param  _data 待写入的数据
*/
void WriteUserFlash(uint32 startAddress,uint16 length,uint8 *_data) /* 写数据到串口屏用户存储区。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x87); /* 写入 0x87，作为对应写数据到串口屏用户存储区的协议字节。 */
    TX_32(startAddress); /* 按 32 位发送用户闪存起始地址。 */
    TX_8N(_data,length); /* 按 8 位元素依次写出「_data,length」 指定的数据及数量。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief  从串口屏用户存储区读取数据
*  \param  startAddress 起始地址
*  \param  length 字节数
*/
void ReadUserFlash(uint32 startAddress,uint16 length) /* 从串口屏用户存储区读取数据。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x88); /* 写入 0x88，作为对应从串口屏用户存储区读取数据的协议字节。 */
    TX_32(startAddress); /* 按 32 位发送用户闪存起始地址。 */
    TX_16(length); /* 按 16 位发送后续数据的长度。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      获取当前画面
*/
void GetScreen(void) /* 获取当前画面。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x01); /* 写入 0x01，作为对应获取当前画面的协议字节。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      设置当前画面
*  \param  screen_id 画面ID
*/
void SetScreen(uint16 screen_id) /* 设置当前画面。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x00); /* 写入 0x00，作为对应设置当前画面的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     禁用\启用画面更新
*  \details 禁用\启用一般成对使用，用于避免闪烁、提高刷新速度
*  \details 用法：
*	\details SetScreenUpdateEnable(0);//禁止更新
*	\details 一系列更新画面的指令
*	\details SetScreenUpdateEnable(1);//立即更新
*  \param  enable 0禁用，1启用
*/
void SetScreenUpdateEnable(uint8 enable) /* 禁用\启用画面更新。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB3); /* 写入 0xB3，作为对应禁用\启用画面更新的协议字节。 */
    TX_8(enable); /* 按 8 位发送启用标记。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     设置控件输入焦点
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  focus 是否具有输入焦点
*/
void SetControlFocus(uint16 screen_id,uint16 control_id,uint8 focus) /* 设置控件输入焦点。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x02); /* 写入 0x02，作为对应设置控件输入焦点的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(focus); /* 按 8 位写出字段 「focus」，让屏幕按约定顺序解释后续命令。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     显示\隐藏控件
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  visible 是否显示
*/
void SetControlVisiable(uint16 screen_id,uint16 control_id,uint8 visible) /* 显示\隐藏控件。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x03); /* 写入 0x03，作为对应显示\隐藏控件的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(visible); /* 按 8 位发送可见标记。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     设置触摸控件使能
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  enable 控件是否使能
*/
void SetControlEnable(uint16 screen_id,uint16 control_id,uint8 enable) /* 设置触摸控件使能。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x04); /* 写入 0x04，作为对应设置触摸控件使能的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(enable); /* 按 8 位发送启用标记。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     设置按钮状态
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  state 单字节按钮状态
*/
void SetButtonValue(uint16 screen_id,uint16 control_id,uchar state) /* 设置按钮状态。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x10); /* 写入 0x10，作为对应设置按钮状态的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(state); /* 按 8 位发送目标状态。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     设置文本值
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  str 文本值
*/
void SetTextValue(uint16 screen_id,uint16 control_id,uchar *str) /* 设置文本值。 */
{
    log_d("SetTextValue: scr=%u ctrl=%u str='%s'", screen_id, control_id, str); /* 以调试级别记录 「"SetTextValue: scr=%u ctrl=%u str='%s'", screen_id, control_id, str」，便于从日志跟踪当前分支。 */
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x10); /* 写入 0x10，作为对应设置文本值的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    SendStrings(str); /* 逐字节发送字符串，遇到 NUL 结束。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

#if FIRMWARE_VER>=908 /* 仅在「FIRMWARE_VER>=908」 成立时编译下方代码。 */
/*!
*  \brief     设置文本为整数值，要求FIRMWARE_VER>=908
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  value 文本数值
*  \param  sign 0-无符号，1-有符号
*  \param  fill_zero 数字位数，不足时左侧补零
*/
void SetTextInt32(uint16 screen_id,uint16 control_id,uint32 value,uint8 sign,uint8 fill_zero) /* 设置文本为整数值，要求FIRMWARE_VER>=908。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x07); /* 写入 0x07，作为对应设置文本为整数值，要求FIRMWARE_VER>=908的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(sign?0X01:0X00); /* 按 8 位写出字段 「sign?0X01:0X00」，让屏幕按约定顺序解释后续命令。 */
    TX_8((fill_zero&0x0f)|0x80); /* 按 8 位写出字段 「(fill_zero&0x0f)|0x80」，让屏幕按约定顺序解释后续命令。 */
    TX_32(value); /* 按 32 位发送控件目标值。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     设置文本单精度浮点值，要求FIRMWARE_VER>=908
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  value 文本数值
*  \param  precision 小数位数
*  \param  show_zeros 为1时，显示末尾0
*/
void SetTextFloat(uint16 screen_id,uint16 control_id,float value,uint8 precision,uint8 show_zeros) /* 设置文本单精度浮点值，要求FIRMWARE_VER>=908。 */
{
	uint8 i = 0; /* 定义i，将i 更新为 「0」。 */

	BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
	TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
	TX_8(0x07); /* 写入 0x07，作为对应设置文本单精度浮点值，要求FIRMWARE_VER>=908的协议字节。 */
	TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
	TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
	TX_8(0x02); /* 写入 0x02，作为对应设置文本单精度浮点值，要求FIRMWARE_VER>=908的协议字节。 */
	TX_8((precision&0x0f)|(show_zeros?0x80:0x00)); /* 按 8 位写出字段 「(precision&0x0f)|(show_zeros?0x80:0x00)」，让屏幕按约定顺序解释后续命令。 */

	for (i=0;i<4;++i) /* 从 「i=0」 开始，在 「i<4」 成立时逐项处理，每轮执行 「++i」。 */
	{
	 //需要区分大小端
#if(0) /* 仅在「(0)」 成立时编译下方代码。 */
		TX_8(((uint8 *)&value)[i]); /* 按 8 位写出字段 「((uint8 *)&value)[i]」，让屏幕按约定顺序解释后续命令。 */
#else /* 前面的编译条件不成立时，改用下方实现或配置。 */
		TX_8(((uint8 *)&value)[3-i]); /* 按 8 位写出字段 「((uint8 *)&value)[3-i]」，让屏幕按约定顺序解释后续命令。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
	}
	END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
/*!
*  \brief      设置进度值
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  value 数值
*/
void SetProgressValue(uint16 screen_id,uint16 control_id,uint32 value) /* 设置进度值。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x10); /* 写入 0x10，作为对应设置进度值的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_32(value); /* 按 32 位发送控件目标值。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     设置仪表值
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  value 数值
*/
void SetMeterValue(uint16 screen_id,uint16 control_id,uint32 value) /* 设置仪表值。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x10); /* 写入 0x10，作为对应设置仪表值的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_32(value); /* 按 32 位发送控件目标值。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     设置仪表值
*  \param  screen_id 画面ID
*  \param  control_id 图片控件ID
*  \param  value 数值
*/
void Set_picMeterValue(uint16 screen_id,uint16 control_id,uint16 value) /* 设置仪表值。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x10); /* 写入 0x10，作为对应设置仪表值的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_16(value); /* 按 16 位发送控件目标值。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      设置滑动条
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  value 数值
*/

void SetSliderValue(uint16 screen_id,uint16 control_id,uint32 value) /* 设置滑动条。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x10); /* 写入 0x10，作为对应设置滑动条的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_32(value); /* 按 32 位发送控件目标值。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      设置选择控件
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  item 当前选项
*/
void SetSelectorValue(uint16 screen_id,uint16 control_id,uint8 item) /* 设置选择控件。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x10); /* 写入 0x10，作为对应设置选择控件的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(item); /* 按 8 位发送选项索引。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     获取控件值
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*/
void GetControlValue(uint16 screen_id,uint16 control_id) /* 获取控件值。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x11); /* 写入 0x11，作为对应获取控件值的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

/*!
*  \brief      开始播放动画
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*/
void AnimationStart(uint16 screen_id,uint16 control_id) /* 开始播放动画。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x20); /* 写入 0x20，作为对应开始播放动画的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

/*!
*  \brief      停止播放动画
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*/
void AnimationStop(uint16 screen_id,uint16 control_id) /* 停止播放动画。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x21); /* 写入 0x21，作为对应停止播放动画的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief      暂停播放动画
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*/
void AnimationPause(uint16 screen_id,uint16 control_id) /* 暂停播放动画。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x22); /* 写入 0x22，作为对应暂停播放动画的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     播放制定帧
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  frame_id 帧ID
*/
void AnimationPlayFrame(uint16 screen_id,uint16 control_id,uint8 frame_id) /* 播放制定帧。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x23); /* 写入 0x23，作为对应播放制定帧的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(frame_id); /* 按 8 位发送动画帧编号。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     播放上一帧
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*/
void AnimationPlayPrev(uint16 screen_id,uint16 control_id) /* 播放上一帧。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x24); /* 写入 0x24，作为对应播放上一帧的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     播放下一帧
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*/
void AnimationPlayNext(uint16 screen_id,uint16 control_id) /* 播放下一帧。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x25); /* 写入 0x25，作为对应播放下一帧的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     曲线控件-添加通道
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  channel 通道号
*  \param  color 颜色
*/
void GraphChannelAdd(uint16 screen_id,uint16 control_id,uint8 channel,uint16 color) /* 曲线控件-添加通道。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x30); /* 写入 0x30，作为对应曲线控件-添加通道的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(channel); /* 按 8 位发送曲线通道编号。 */
    TX_16(color); /* 按 16 位发送颜色值。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     曲线控件-删除通道
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  channel 通道号
*/
void GraphChannelDel(uint16 screen_id,uint16 control_id,uint8 channel) /* 曲线控件-删除通道。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x31); /* 写入 0x31，作为对应曲线控件-删除通道的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(channel); /* 按 8 位发送曲线通道编号。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     曲线控件-添加数据
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  channel 通道号
*  \param  pData 曲线数据
*  \param  nDataLen 数据个数
*/
void GraphChannelDataAdd(uint16 screen_id,uint16 control_id,uint8 channel,uint8 *pData,uint16 nDataLen) /* 曲线控件-添加数据。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x32); /* 写入 0x32，作为对应曲线控件-添加数据的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(channel); /* 按 8 位发送曲线通道编号。 */
    TX_16(nDataLen); /* 按 16 位发送数组长度。 */
    TX_8N(pData,nDataLen); /* 按 8 位元素依次写出「pData,nDataLen」 指定的数据及数量。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     曲线控件-清除数据
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  channel 通道号
*/
void GraphChannelDataClear(uint16 screen_id,uint16 control_id,uint8 channel) /* 曲线控件-清除数据。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x33); /* 写入 0x33，作为对应曲线控件-清除数据的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(channel); /* 按 8 位发送曲线通道编号。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     曲线控件-设置视图窗口
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  x_offset 水平偏移
*  \param  x_mul 水平缩放系数
*  \param  y_offset 垂直偏移
*  \param  y_mul 垂直缩放系数
*/
void GraphSetViewport(uint16 screen_id,uint16 control_id,int16 x_offset,uint16 x_mul,int16 y_offset,uint16 y_mul) /* 曲线控件-设置视图窗口。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x34); /* 写入 0x34，作为对应曲线控件-设置视图窗口的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_16(x_offset); /* 按 16 位写出字段 「x_offset」，多字节高位先发以符合屏幕协议。 */
    TX_16(x_mul); /* 按 16 位写出字段 「x_mul」，多字节高位先发以符合屏幕协议。 */
    TX_16(y_offset); /* 按 16 位写出字段 「y_offset」，多字节高位先发以符合屏幕协议。 */
    TX_16(y_mul); /* 按 16 位写出字段 「y_mul」，多字节高位先发以符合屏幕协议。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     发送批量更新帧的开头。接着调用 BatchSet 系列追加控件，最后用 BatchEnd 发帧尾。
*  \param  screen_id 画面ID
*/
void BatchBegin(uint16 screen_id) /* 发送批量更新帧的开头。接着调用 BatchSet 系列追加控件，最后用 BatchEnd 发帧尾。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x12); /* 写入 0x12，作为对应发送批量更新帧的开头。接着调用 BatchSet 系列追加控件，最后用 BatchEnd 发帧尾的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
}
/*!
*  \brief     批量更新按钮控件
*  \param  control_id 控件ID
*  \param  state 按钮状态，写入当前批量更新帧
*/
void BatchSetButtonValue(uint16 control_id,uint8 state) /* 批量更新按钮控件。 */
{
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_16(1); /* 按 16 位写出字段 「1」，多字节高位先发以符合屏幕协议。 */
    TX_8(state); /* 按 8 位发送目标状态。 */
}
/*!
*  \brief     批量更新进度条控件
*  \param  control_id 控件ID
*  \param  value 数值
*/
void BatchSetProgressValue(uint16 control_id,uint32 value) /* 批量更新进度条控件。 */
{
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_16(4); /* 按 16 位写出字段 「4」，多字节高位先发以符合屏幕协议。 */
    TX_32(value); /* 按 32 位发送控件目标值。 */
}

/*!
*  \brief     批量更新滑动条控件
*  \param  control_id 控件ID
*  \param  value 数值
*/
void BatchSetSliderValue(uint16 control_id,uint32 value) /* 批量更新滑动条控件。 */
{
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_16(4); /* 按 16 位写出字段 「4」，多字节高位先发以符合屏幕协议。 */
    TX_32(value); /* 按 32 位发送控件目标值。 */
}
/*!
*  \brief     批量更新仪表控件
*  \param  control_id 控件ID
*  \param  value 数值
*/
void BatchSetMeterValue(uint16 control_id,uint32 value) /* 批量更新仪表控件。 */
{
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_16(4); /* 按 16 位写出字段 「4」，多字节高位先发以符合屏幕协议。 */
    TX_32(value); /* 按 32 位发送控件目标值。 */
}
/*!
*  \brief  计算字节长度，扫描到第一个 NUL 结束。
*  \param  str 指向有效的 NUL 结尾字符串
*  \return 不含 NUL 的字节数
*/
uint32 GetStringLen(uchar *str) /* 计算字节长度，扫描到第一个 NUL 结束。 */
{
    uchar *p = str; /* 定义p，将p 更新为 「str」。 */
    while(*str) /* 在 「*str」 成立期间重复处理循环体。 */
    {
        str++; /* 将str加一，推进处理进度。 */
    }

    return (str-p); /* 将 「(str-p)」 返回给调用者，供上层使用。 */
}
/*!
*  \brief     批量更新文本控件
*  \param  control_id 控件ID
*  \param  strings 字符串
*/
void BatchSetText(uint16 control_id,uchar *strings) /* 批量更新文本控件。 */
{
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_16(GetStringLen(strings)); /* 按 16 位写出字段 「GetStringLen(strings)」，多字节高位先发以符合屏幕协议。 */
    SendStrings(strings); /* 逐字节发送字符串，遇到 NUL 结束。 */
}
/*!
*  \brief     批量更新动画\图标控件
*  \param  control_id 控件ID
*  \param  frame_id 帧ID
*/
void BatchSetFrame(uint16 control_id,uint16 frame_id) /* 批量更新动画\图标控件。 */
{
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_16(2); /* 按 16 位写出字段 「2」，多字节高位先发以符合屏幕协议。 */
    TX_16(frame_id); /* 按 16 位发送动画帧编号。 */
}

#if FIRMWARE_VER>=908 /* 仅在「FIRMWARE_VER>=908」 成立时编译下方代码。 */

/*!
*  \brief     批量设置控件可见
*  \param  control_id 控件ID
*  \param  visible 帧ID
*/
void BatchSetVisible(uint16 control_id,uint8 visible) /* 批量设置控件可见。 */
{
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(1); /* 按 8 位写出字段 「1」，让屏幕按约定顺序解释后续命令。 */
    TX_8(visible); /* 按 8 位发送可见标记。 */
}
/*!
*  \brief     批量设置控件使能
*  \param  control_id 控件ID
*  \param  enable 帧ID
*/
void BatchSetEnable(uint16 control_id,uint8 enable) /* 批量设置控件使能。 */
{
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(2); /* 按 8 位写出字段 「2」，让屏幕按约定顺序解释后续命令。 */
    TX_8(enable); /* 按 8 位发送启用标记。 */
}

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
/*!
*  \brief    结束批量更新
*/
void BatchEnd() /* 结束批量更新。 */
{
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     设置倒计时控件
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  timeout 倒计时(秒)
*/
void SeTimer(uint16 screen_id,uint16 control_id,uint32 timeout) /* 设置倒计时控件。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x40); /* 写入 0x40，作为对应设置倒计时控件的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_32(timeout); /* 按 32 位写出字段 「timeout」，多字节高位先发以符合屏幕协议。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     开启倒计时控件
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*/
void StartTimer(uint16 screen_id,uint16 control_id) /* 开启倒计时控件。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x41); /* 写入 0x41，作为对应开启倒计时控件的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     停止倒计时控件
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*/
void StopTimer(uint16 screen_id,uint16 control_id) /* 停止倒计时控件。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x42); /* 写入 0x42，作为对应停止倒计时控件的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     暂停倒计时控件
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*/
void PauseTimer(uint16 screen_id,uint16 control_id) /* 暂停倒计时控件。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x44); /* 写入 0x44，作为对应暂停倒计时控件的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     设置控件背景色
*  \details  支持控件：进度条、文本
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  color 背景色
*/
void SetControlBackColor(uint16 screen_id,uint16 control_id,uint16 color) /* 设置控件背景色。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x18); /* 写入 0x18，作为对应设置控件背景色的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_16(color); /* 按 16 位发送颜色值。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     设置控件前景色
* \details  支持控件：进度条
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  color 前景色
*/
void SetControlForeColor(uint16 screen_id,uint16 control_id,uint16 color) /* 设置控件前景色。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x19); /* 写入 0x19，作为对应设置控件前景色的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_16(color); /* 按 16 位发送颜色值。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     显示\隐藏弹出菜单控件
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  show 是否显示，为0时focus_control_id无效
*  \param  focus_control_id 关联的文本控件(菜单控件的内容输出到文本控件)
*/
void ShowPopupMenu(uint16 screen_id,uint16 control_id,uint8 show,uint16 focus_control_id) /* 显示\隐藏弹出菜单控件。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x13); /* 写入 0x13，作为对应显示\隐藏弹出菜单控件的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(show); /* 按 8 位写出字段 「show」，让屏幕按约定顺序解释后续命令。 */
    TX_16(focus_control_id); /* 按 16 位写出字段 「focus_control_id」，多字节高位先发以符合屏幕协议。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     显示\隐藏系统键盘
*  \param  show 0隐藏，1显示
*  \param  x 键盘显示位置X坐标
*  \param  y 键盘显示位置Y坐标
*  \param  type 0小键盘，1全键盘
*  \param  option 0正常字符，1密码，2时间设置
*  \param  max_len 键盘录入字符长度限制
*/
void ShowKeyboard(uint8 show,uint16 x,uint16 y,uint8 type,uint8 option,uint8 max_len) /* 显示\隐藏系统键盘。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x86); /* 写入 0x86，作为对应显示\隐藏系统键盘的协议字节。 */
    TX_8(show); /* 按 8 位写出字段 「show」，让屏幕按约定顺序解释后续命令。 */
    TX_16(x); /* 按 16 位发送横坐标。 */
    TX_16(y); /* 按 16 位发送纵坐标。 */
    TX_8(type); /* 按 8 位写出字段 「type」，让屏幕按约定顺序解释后续命令。 */
    TX_8(option); /* 按 8 位写出字段 「option」，让屏幕按约定顺序解释后续命令。 */
    TX_8(max_len); /* 按 8 位写出字段 「max_len」，让屏幕按约定顺序解释后续命令。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

#if FIRMWARE_VER>=921 /* 仅在「FIRMWARE_VER>=921」 成立时编译下方代码。 */
/*!
*  \brief     多语言设置
*  \param  ui_lang 用户界面语言0~9
*  \param  sys_lang 系统键盘语言-0中文，1英文
*/
void SetLanguage(uint8 ui_lang,uint8 sys_lang) /* 多语言设置。 */
{
    uint8 lang = ui_lang; /* 定义lang，将lang 更新为 「ui_lang」。 */
    if(sys_lang)   lang |= 0x80; /* 当 「sys_lang) lang |= 0x80」 成立时处理下方分支。 */

    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xC1); /* 写入 0xC1，作为对应多语言设置的协议字节。 */
    TX_8(lang); /* 按 8 位写出字段 「lang」，让屏幕按约定顺序解释后续命令。 */
    TX_8(0xC1+lang);//校验，防止意外修改语言
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */


#if FIRMWARE_VER>=921 /* 仅在「FIRMWARE_VER>=921」 成立时编译下方代码。 */
/*!
*  \brief     开始保存控件数值到FLASH
*  \param  version 数据版本号，可任意指定，高16位为主版本号，低16位为次版本号
*  \param  address 数据在用户存储区的存放地址，注意防止地址重叠、冲突
*/
void FlashBeginSaveControl(uint32 version,uint32 address) /* 开始保存控件数值到FLASH。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0xAA); /* 写入 0xAA，作为对应开始保存控件数值到FLASH的协议字节。 */
    TX_32(version); /* 按 32 位写出字段 「version」，多字节高位先发以符合屏幕协议。 */
    TX_32(address); /* 按 32 位写出字段 「address」，多字节高位先发以符合屏幕协议。 */
}

/*!
*  \brief     保存某个控件的数值到FLASH
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*/
void FlashSaveControl(uint16 screen_id,uint16 control_id) /* 保存某个控件的数值到FLASH。 */
{
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
}
/*!
*  \brief     保存某个控件的数值到FLASH
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*/
void FlashEndSaveControl() /* 保存某个控件的数值到FLASH。 */
{
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     从FLASH中恢复控件数据
*  \param  version 数据版本号，主版本号必须与存储时一致，否则会加载失败
*  \param  address 数据在用户存储区的存放地址
*/
void FlashRestoreControl(uint32 version,uint32 address) /* 从FLASH中恢复控件数据。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0xAB); /* 写入 0xAB，作为对应从FLASH中恢复控件数据的协议字节。 */
    TX_32(version); /* 按 32 位写出字段 「version」，多字节高位先发以符合屏幕协议。 */
    TX_32(address); /* 按 32 位写出字段 「address」，多字节高位先发以符合屏幕协议。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

#if FIRMWARE_VER>=921 /* 仅在「FIRMWARE_VER>=921」 成立时编译下方代码。 */
/*!
*  \brief     设置历史曲线采样数据值(单字节，uint8或int8)
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  value 采样点数据
*  \param  channel 通道数
*/
void HistoryGraph_SetValueInt8(uint16 screen_id,uint16 control_id,uint8 *value,uint8 channel) /* 设置历史曲线采样数据值(单字节，uint8或int8)。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x60); /* 写入 0x60，作为对应设置历史曲线采样数据值(单字节，uint8或int8)的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8N(value,channel); /* 按 8 位元素依次写出「value,channel」 指定的数据及数量。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     设置历史曲线采样数据值(双字节，uint16或int16)
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  value 采样点数据
*  \param  channel 通道数
*/
void HistoryGraph_SetValueInt16(uint16 screen_id,uint16 control_id,uint16 *value,uint8 channel) /* 设置历史曲线采样数据值(双字节，uint16或int16)。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x60); /* 写入 0x60，作为对应设置历史曲线采样数据值(双字节，uint16或int16)的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_16N(value,channel); /* 按 16 位元素依次写出「value,channel」 指定的数据及数量。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     设置历史曲线采样数据值(四字节，uint32或int32)
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  value 采样点数据
*  \param  channel 通道数
*/
void HistoryGraph_SetValueInt32(uint16 screen_id,uint16 control_id,uint32 *value,uint8 channel) /* 设置历史曲线采样数据值(四字节，uint32或int32)。 */
{
    uint8 i = 0; /* 定义i，将i 更新为 「0」。 */

    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x60); /* 写入 0x60，作为对应设置历史曲线采样数据值(四字节，uint32或int32)的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */

    for (;i<channel;++i) /* 沿用当前下标，在 「i<channel」 成立时逐项处理，每轮执行 「++i」。 */
    {
        TX_32(value[i]); /* 按 32 位写出字段 「value[i]」，多字节高位先发以符合屏幕协议。 */
    }

    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     设置历史曲线采样数据值(单精度浮点数)
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  value 采样点数据
*  \param  channel 通道数
*/
void HistoryGraph_SetValueFloat(uint16 screen_id,uint16 control_id,float *value,uint8 channel) /* 设置历史曲线采样数据值(单精度浮点数)。 */
{
    uint8 i = 0; /* 定义i，将i 更新为 「0」。 */
    uint32 tmp = 0; /* 定义tmp，将tmp 更新为 「0」。 */

    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x60); /* 写入 0x60，作为对应设置历史曲线采样数据值(单精度浮点数)的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */

    for (;i<channel;++i) /* 沿用当前下标，在 「i<channel」 成立时逐项处理，每轮执行 「++i」。 */
    {
        tmp = *(uint32 *)(value+i); /* 将tmp 更新为 「*(uint32 *)(value+i)」。 */
        TX_32(tmp); /* 按 32 位写出字段 「tmp」，多字节高位先发以符合屏幕协议。 */
    }

    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     允许或禁止历史曲线采样
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  enable 0-禁止，1-允许
*/
void HistoryGraph_EnableSampling(uint16 screen_id,uint16 control_id,uint8 enable) /* 允许或禁止历史曲线采样。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x61); /* 写入 0x61，作为对应允许或禁止历史曲线采样的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(enable); /* 按 8 位发送启用标记。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     显示或隐藏历史曲线通道
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  channel 通道编号
*  \param  show 0-隐藏，1-显示
*/
void HistoryGraph_ShowChannel(uint16 screen_id,uint16 control_id,uint8 channel,uint8 show) /* 显示或隐藏历史曲线通道。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x62); /* 写入 0x62，作为对应显示或隐藏历史曲线通道的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(channel); /* 按 8 位发送曲线通道编号。 */
    TX_8(show); /* 按 8 位写出字段 「show」，让屏幕按约定顺序解释后续命令。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     设置历史曲线时间长度(即采样点数)
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  sample_count 一屏显示的采样点数
*/
void HistoryGraph_SetTimeLength(uint16 screen_id,uint16 control_id,uint16 sample_count) /* 设置历史曲线时间长度(即采样点数)。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x63); /* 写入 0x63，作为对应设置历史曲线时间长度(即采样点数)的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(0x00); /* 写入 0x00，作为对应设置历史曲线时间长度(即采样点数)的协议字节。 */
    TX_16(sample_count); /* 按 16 位写出字段 「sample_count」，多字节高位先发以符合屏幕协议。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

/*!
*  \brief     历史曲线缩放到全屏
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*/
void HistoryGraph_SetTimeFullScreen(uint16 screen_id,uint16 control_id) /* 历史曲线缩放到全屏。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x63); /* 写入 0x63，作为对应历史曲线缩放到全屏的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(0x01); /* 写入 0x01，作为对应历史曲线缩放到全屏的协议字节。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     设置历史曲线缩放比例系数
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  zoom 缩放百分比(zoom>100%时水平方向缩小，反正放大)
*  \param  max_zoom 缩放限制，一屏最多显示采样点数
*  \param  min_zoom 缩放限制，一屏最少显示采样点数
*/
void HistoryGraph_SetTimeZoom(uint16 screen_id,uint16 control_id,uint16 zoom,uint16 max_zoom,uint16 min_zoom) /* 设置历史曲线缩放比例系数。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x63); /* 写入 0x63，作为对应设置历史曲线缩放比例系数的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_8(0x02); /* 写入 0x02，作为对应设置历史曲线缩放比例系数的协议字节。 */
    TX_16(zoom); /* 按 16 位写出字段 「zoom」，多字节高位先发以符合屏幕协议。 */
    TX_16(max_zoom); /* 按 16 位写出字段 「max_zoom」，多字节高位先发以符合屏幕协议。 */
    TX_16(min_zoom); /* 按 16 位写出字段 「min_zoom」，多字节高位先发以符合屏幕协议。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

#if SD_FILE_EN /* 仅在「SD_FILE_EN」 成立时编译下方代码。 */
/*!
*  \brief     检测SD卡是否插入
*/
void SD_IsInsert(void) /* 检测SD卡是否插入。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x36); /* 写入 0x36，作为对应检测SD卡是否插入的协议字节。 */
    TX_8(0x01); /* 写入 0x01，作为对应检测SD卡是否插入的协议字节。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     打开或创建文件
*  \param  filename 文件名称(仅ASCII编码)
*  \param  mode 模式，可选组合模式如上FA_XXXX
*/
void SD_CreateFile(uint8 *filename,uint8 mode) /* 打开或创建文件。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x36); /* 写入 0x36，作为对应打开或创建文件的协议字节。 */
    TX_8(0x05); /* 写入 0x05，作为对应打开或创建文件的协议字节。 */
    TX_8(mode); /* 按 8 位发送工作模式。 */
    SendStrings(filename); /* 逐字节发送字符串，遇到 NUL 结束。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     以当前时间创建文件，例如:20161015083000.txt
*  \param  ext 文件后缀，例如 txt
*/
void SD_CreateFileByTime(uint8 *ext) /* 以当前时间创建文件，例如:20161015083000.txt。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x36); /* 写入 0x36，作为对应以当前时间创建文件，例如:20161015083000.txt的协议字节。 */
    TX_8(0x02); /* 写入 0x02，作为对应以当前时间创建文件，例如:20161015083000.txt的协议字节。 */
    SendStrings(ext); /* 逐字节发送字符串，遇到 NUL 结束。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     在当前文件末尾写入数据
*  \param  buffer 数据
*  \param  dlc 数据长度
*/
void SD_WriteFile(uint8 *buffer,uint16 dlc) /* 在当前文件末尾写入数据。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x36); /* 写入 0x36，作为对应在当前文件末尾写入数据的协议字节。 */
    TX_8(0x03); /* 写入 0x03，作为对应在当前文件末尾写入数据的协议字节。 */
    TX_16(dlc); /* 按 16 位写出字段 「dlc」，多字节高位先发以符合屏幕协议。 */
    TX_8N(buffer,dlc); /* 按 8 位元素依次写出「buffer,dlc」 指定的数据及数量。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     读取当前文件
*  \param  offset 文件位置偏移
*  \param  dlc 数据长度
*/
void SD_ReadFile(uint32 offset,uint16 dlc) /* 读取当前文件。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x36); /* 写入 0x36，作为对应读取当前文件的协议字节。 */
    TX_8(0x07); /* 写入 0x07，作为对应读取当前文件的协议字节。 */
    TX_32(offset); /* 按 32 位写出字段 「offset」，多字节高位先发以符合屏幕协议。 */
    TX_16(dlc); /* 按 16 位写出字段 「dlc」，多字节高位先发以符合屏幕协议。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

/*!
*  \brief     获取当前文件长度
*/
void SD_GetFileSize() /* 获取当前文件长度。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x36); /* 写入 0x36，作为对应获取当前文件长度的协议字节。 */
    TX_8(0x06); /* 写入 0x06，作为对应获取当前文件长度的协议字节。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     关闭当前文件
*/
void SD_CloseFile() /* 关闭当前文件。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x36); /* 写入 0x36，作为对应关闭当前文件的协议字节。 */
    TX_8(0x04); /* 写入 0x04，作为对应关闭当前文件的协议字节。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */ //SD_FILE_EN
/*!
*  \brief     记录控件-触发警告
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  value 告警值
*  \param  time 告警产生的时间，为0时使用屏幕内部时间
*/
void Record_SetEvent(uint16 screen_id,uint16 control_id,uint16 value,uint8 *time) /* 记录控件-触发警告。 */
{
    uint8 i  = 0; /* 定义i，将i 更新为 「0」。 */

    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x50); /* 写入 0x50，作为对应记录控件-触发警告的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_16(value); /* 按 16 位发送控件目标值。 */

    if(time) /* 当 「time」 成立时处理下方分支。 */
    {
        for(i=0;i<7;++i) /* 从 「i=0」 开始，在 「i<7」 成立时逐项处理，每轮执行 「++i」。 */
            TX_8(time[i]); /* 按 8 位写出字段 「time[i]」，让屏幕按约定顺序解释后续命令。 */
    }

    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     记录控件-解除警告
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  value 告警值
*  \param  time 告警解除的时间，为0时使用屏幕内部时间
*/
void Record_ResetEvent(uint16 screen_id,uint16 control_id,uint16 value,uint8 *time) /* 记录控件-解除警告。 */
{
    uint8 i  = 0; /* 定义i，将i 更新为 「0」。 */

    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x51); /* 写入 0x51，作为对应记录控件-解除警告的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_16(value); /* 按 16 位发送控件目标值。 */

    if(time) /* 当 「time」 成立时处理下方分支。 */
    {
        for(i=0;i<7;++i) /* 从 「i=0」 开始，在 「i<7」 成立时逐项处理，每轮执行 「++i」。 */
            TX_8(time[i]); /* 按 8 位写出字段 「time[i]」，让屏幕按约定顺序解释后续命令。 */
    }

    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief    记录控件- 添加常规记录
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  record 一条记录(字符串)，子项通过分号隔开，例如：第一项;第二项;第三项;
*/
void Record_Add(uint16 screen_id,uint16 control_id,uint8 *record) /* 记录控件- 添加常规记录。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x52); /* 写入 0x52，作为对应记录控件- 添加常规记录的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */

    SendStrings(record); /* 逐字节发送字符串，遇到 NUL 结束。 */

    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     记录控件-清除记录数据
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*/
void Record_Clear(uint16 screen_id,uint16 control_id) /* 记录控件-清除记录数据。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x53); /* 写入 0x53，作为对应记录控件-清除记录数据的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     记录控件-设置记录显示偏移
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*  \param  offset 显示偏移，滚动条位置
*/
void Record_SetOffset(uint16 screen_id,uint16 control_id,uint16 offset) /* 记录控件-设置记录显示偏移。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x54); /* 写入 0x54，作为对应记录控件-设置记录显示偏移的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    TX_16(offset); /* 按 16 位写出字段 「offset」，多字节高位先发以符合屏幕协议。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     记录控件-获取当前记录数目
*  \param  screen_id 画面ID
*  \param  control_id 控件ID
*/
void Record_GetCount(uint16 screen_id,uint16 control_id) /* 记录控件-获取当前记录数目。 */
{
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0xB1); /* 按 8 位发送扩展界面操作命令族的标识。 */
    TX_8(0x55); /* 写入 0x55，作为对应记录控件-获取当前记录数目的协议字节。 */
    TX_16(screen_id); /* 按 16 位发送画面 ID，用于定位所属页面。 */
    TX_16(control_id); /* 按 16 位发送控件 ID，用于在画面中定位控件。 */
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}
/*!
*  \brief     向屏幕提交 0x82 读时命令，响应由 HMI 接收任务处理。
*  \return 1 表示已提交给 DMA，0 表示本轮未提交。
*/
uint8 ReadRTC(void) /* 提交一条屏幕读时命令，由主循环继续等待回包。 */
{
    /* 屏幕读时指令为 0x82，只有命令字。CRC 打开时预留两字节，最后拼接屏幕帧尾。 */
    uint8 frame[] = { /* 定义帧，将帧 更新为 「{」。 */
        0xEE, 0x82, /* 用 「0xEE, 0x82,」 补足上行的表达式、缓冲区内容或接口参数。 */
#if(CRC16_ENABLE) /* 仅在「(CRC16_ENABLE)」 成立时编译下方代码。 */
        0x00, 0x00, /* 用 「0x00, 0x00,」 补足上行的表达式、缓冲区内容或接口参数。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
        0xFF, 0xFC, 0xFF, 0xFF /* 写入屏幕协议规定的四字节帧尾，让接收方识别读时命令的结束。 */
    };

    /* 屏幕串口正发送其它数据时立即返回，让 RTC 任务下一轮再试。 */
    if (USART2_DMA_IsTxBusy()) /* 屏幕串口正发送其它数据时立即返回，让 RTC 任务下一轮再试。 */
        return 0U; /* 以 0U 返回本分支的结果，调用方据此处理当前状态。 */

#if(CRC16_ENABLE) /* 仅在「(CRC16_ENABLE)」 成立时编译下方代码。 */
    {
        uint16 crc = 0xFFFF; /* 定义crc，将crc 更新为 「0xFFFF」。 */
        /* 此帧没有参数，因此 CRC 只覆盖命令字，并按高字节在前写入。 */
        AddCRC16(frame + 1, 1, &crc); /* 将指定字节纳入 CRC-16 累积值。 */
        frame[2] = (uint8)(crc >> 8); /* 将帧 更新为 「(uint8)(crc >> 8)」。 */
        frame[3] = (uint8)crc; /* 将帧 更新为 「(uint8)crc」。 */
    }
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

    /* DMA 发送函数会把局部帧复制到驱动自己的缓冲区。返回 1 后才开始等屏幕回包。 */
    return USART2_DMA_SendData(frame, sizeof(frame)) == USART_DMA_OK; /* 将 「USART2_DMA_SendData(frame, sizeof(frame)) == USART_DMA_OK」 返回给调用者，供上层使用。 */
}

/*!
*  \brief     设置屏幕RTC时间 (指令0x83)
*  \param    rtc_data 8字节时间数组 (二进制格式, 大端序):
*            [0~1] 年(uint16), [2]月, [3]日, [4]时, [5]分, [6]秒, [7]星期
*  \note     与屏幕ReadRTC返回的BCD格式不同, 本函数发送的是二进制值
*  \note     大彩协议0x83: EE 83 YY_hi YY_lo MM DD HH MM_ SS WK FF FC FF FF
*            其中星期: 0x00=周日, 0x01~0x06=周一~周六
*/
void WriteRTC(uint8 *rtc_data) /* 设置屏幕RTC时间 (指令0x83)。 */
{
    uint8 i; /* 声明i，为后续计算或读取保存中间值。 */
    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    TX_8(0x83); /* 写入 0x83，作为对应设置屏幕RTC时间 (指令0x83)的协议字节。 */
    for (i = 0; i < 8; i++) /* 从 「i = 0」 开始，在 「i < 8」 成立时逐项处理，每轮执行 「i++」。 */
    {
        TX_8(rtc_data[i]); /* 按 8 位写出字段 「rtc_data[i]」，让屏幕按约定顺序解释后续命令。 */
    }
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

/*!
*  \brief   播放音乐
*  \param   buffer 已编码的 19 字节命令内容，调用方提供完整缓冲。
*           空指针时只发送帧头与帧尾。
*/
void PlayMusic(uint8 *buffer) /* 播放音乐。 */
{
    uint8 i  = 0; /* 定义i，将i 更新为 「0」。 */

    BEGIN_CMD(); /* 写入触摸屏帧头，标记命令起始。 */
    if(buffer) /* 当 「buffer」 成立时处理下方分支。 */
    {
        for(i=0;i<19;++i) /* 从 「i=0」 开始，在 「i<19」 成立时逐项处理，每轮执行 「++i」。 */
            TX_8(buffer[i]); /* 按 8 位写出字段 「buffer[i]」，让屏幕按约定顺序解释后续命令。 */
    }
    END_CMD(); /* 写入触摸屏帧尾，标记命令结束。 */
}

