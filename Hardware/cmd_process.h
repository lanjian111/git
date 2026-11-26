/*! 
*  \file cmd_process.h
*  \brief 指令分类处理
*  \version 1.0
*  \date 2012-2018
*  \copyright 广州大彩光电科技有限公司
*/


#ifndef CMD_PROCESS_H /* 仅在尚未定义 CMD_PROCESS_H时编译下方代码。 */
#define CMD_PROCESS_H /* 定义 CMD_PROCESS_H，供头文件防重包含或后续条件编译检查。 */
#include "hmi_driver.h" /* 取得触摸屏命令封装和链路处理接口。 */
#include "stm32f10x.h" /* 使用 STM32F1 的寄存器、外设类型和基于工程配置启用的驱动接口。 */

#define NOTIFY_TOUCH_PRESS         0X01  //触摸屏按下通知
#define NOTIFY_TOUCH_RELEASE       0X03  //触摸屏松开通知
#define NOTIFY_WRITE_FLASH_OK      0X0C  //写FLASH成功
#define NOTIFY_WRITE_FLASH_FAILD   0X0D  //写FLASH失败
#define NOTIFY_READ_FLASH_OK       0X0B  //读FLASH成功
#define NOTIFY_READ_FLASH_FAILD    0X0F  //读FLASH失败
#define NOTIFY_MENU                0X14  //菜单事件通知
#define NOTIFY_TIMER               0X43  //定时器超时通知
#define NOTIFY_CONTROL             0XB1  //控件更新通知
#define NOTIFY_READ_RTC            0XF7  // 屏幕读时应答的命令字，位于帧偏移 1
#define MSG_GET_CURRENT_SCREEN     0X01  //画面ID变化通知
#define MSG_GET_DATA               0X11  //控件数据通知

#define PTR2U16(PTR) ((((uint8 *)(PTR))[0]<<8)|((uint8 *)(PTR))[1])  // 读取 2 字节大端值，调用方应先检查长度
#define PTR2U32(PTR) ((((uint8 *)(PTR))[0]<<24)|(((uint8 *)(PTR))[1]<<16)|(((uint8 *)(PTR))[2]<<8)|((uint8 *)(PTR))[3])  // 读取 4 字节大端值，调用方应先检查长度


enum CtrlType /* 定义屏幕控件类型编号，消息分发器按它选择对应回调。 */
{
    kCtrlUnknown=0x0, /* 将kCtrlUnknown 更新为 「0x0,」。 */
    kCtrlButton=0x10,                     //按钮
    kCtrlText,                            //文本
    kCtrlProgress,                        //进度条
    kCtrlSlider,                          //滑动条
    kCtrlMeter,                            //仪表
    kCtrlDropList,                        //下拉列表
    kCtrlAnimation,                       //动画
    kCtrlRTC,                             //时间显示
    kCtrlGraph,                           //曲线图控件
    kCtrlTable,                           //表格控件
    kCtrlMenu,                            //菜单控件
    kCtrlSelector,                        //选择控件
    kCtrlQRCode,                          //二维码
};

#pragma pack(push) /* 按 pack(push) 调整编译器对附近代码的处理。 */
#pragma pack(1)                           //按字节对齐

/* 屏幕通用控件帧的前 8 字节映射。param 的有效范围由实际帧长决定，
 * 线上帧尾紧随有效参数，结构体末尾的 cmd_tail 成员不用于定位它。 */
typedef struct /* 定义关联字段的结构类型，供相关接口复用。 */
{
    uint8    cmd_head;                    //帧头

    uint8    cmd_type;                    //命令类型(UPDATE_CONTROL)    
    uint8    ctrl_msg;                    //CtrlMsgType-指示消息的类型
    uint16   screen_id;                   //产生消息的画面ID
    uint16   control_id;                  //产生消息的控件ID
    uint8    control_type;                //控件类型

    uint8    param[256];                  // 可变长度参数，当前接收上限另由 CMD_MAX_SIZE 限制

    uint8  cmd_tail[4];                   //帧尾
}CTRL_MSG,*PCTRL_MSG; /* 为打包后的消息结构及其指针定义别名，使组帧缓冲区能统一交给分发接口。 */

#pragma pack(pop) /* 按 pack(pop) 调整编译器对附近代码的处理。 */


/*! 
*  \brief  消息处理流程
*  \param msg 已由队列切帧的可写缓冲区。处理时会就地转换页面和控件 ID。
*  \param size queue_find_cmd 返回的长度，单位为字节
*/
void ProcessMessage( PCTRL_MSG msg, uint16 size ); /* 声明接口，按长度和消息类型校验并分发屏幕回包。 */
/*! 
*  \brief  画面切换通知
*  \details  当前画面改变时(或调用GetScreen)，执行此函数
*  \param screen_id 当前画面ID
*/
void NotifyScreen(uint16 screen_id); /* 声明接口，记录触摸屏报告的当前画面编号，便于调试页面切换。 */
/*! 
*  \brief  触摸坐标事件响应
*  \param press 1按下触摸屏，3松开触摸屏
*  \param x x坐标
*  \param y y坐标
*/
void NotifyTouchXY(uint8 press,uint16 x,uint16 y); /* 声明接口，触摸坐标事件响应。 */

/*! 
*  \brief  按钮控件通知
*  \details  当按钮状态改变(或调用GetControlValue)时，执行此函数
*  \param screen_id 画面ID
*  \param control_id 控件ID
*  \param state 按钮状态：0弹起，1按下
*/
void NotifyButton(uint16 screen_id, uint16 control_id, uint8 state); /* 声明接口，按画面编号把按钮动作分发给清洗或萃取的业务处理。 */
/*! 
*  \brief  文本控件通知
*  \details  当文本通过键盘更新(或调用GetControlValue)时，执行此函数
*  \details  文本控件的内容以字符串形式下发到MCU，如果文本控件内容是浮点值，
*  \details  则需要在此函数中将下发字符串重新转回浮点值。
*  \param screen_id 画面ID
*  \param control_id 控件ID
*  \param str 文本控件内容
*/
void NotifyText(uint16 screen_id, uint16 control_id, uint8 *str); /* 声明接口，在萃取页面将文本控件的数据交给萃取参数更新函数。 */
/*!                                                                              
*  \brief  进度条控件通知                                                       
*  \details  调用GetControlValue时，执行此函数                                  
*  \param screen_id 画面ID                                                      
*  \param control_id 控件ID                                                     
*  \param value 值                                                              
*/   
void NotifyProgress(uint16 screen_id, uint16 control_id, uint32 value); /* 声明接口，进度条控件通知。 */
/*!                                                                              
*  \brief  滑动条控件通知                                                       
*  \details  当滑动条改变(或调用GetControlValue)时，执行此函数                  
*  \param screen_id 画面ID                                                      
*  \param control_id 控件ID                                                     
*  \param value 值                                                              
*/    
void NotifySlider(uint16 screen_id, uint16 control_id, uint32 value); /* 声明接口，滑动条控件通知。 */
/*! 
*  \brief  仪表控件通知
*  \details  调用GetControlValue时，执行此函数
*  \param screen_id 画面ID
*  \param control_id 控件ID
*  \param value 值
*/
void NotifyMeter(uint16 screen_id, uint16 control_id, uint32 value); /* 声明接口，仪表控件通知。 */
/*! 
*  \brief  菜单控件通知
*  \details  当菜单项按下或松开时，执行此函数
*  \param screen_id 画面ID
*  \param control_id 控件ID
*  \param item 菜单项索引
*  \param state 按钮状态：0松开，1按下
*/
void NotifyMenu(uint16 screen_id, uint16 control_id, uint8  item, uint8  state); /* 声明接口，菜单控件通知。 */

/*! 
*  \brief  选择控件通知
*  \details  当选择控件变化时，执行此函数
*  \param screen_id 画面ID
*  \param control_id 控件ID
*  \param item 当前选项
*/
void NotifySelector(uint16 screen_id, uint16 control_id, uint8  item); /* 声明接口，读取萃取页面的排液选项并更新业务模式。 */
/*! 
*  \brief  定时器超时通知处理
*  \param screen_id 画面ID
*  \param control_id 控件ID
*/
void NotifyTimer(uint16 screen_id, uint16 control_id); /* 声明接口，定时器超时通知处理。 */
/*! 
*  \brief  读取用户FLASH状态返回
*  \param status 0失败，1成功
*  \param _data 返回数据
*  \param length 数据长度
*/
void NotifyReadFlash(uint8 status,uint8 *_data,uint16 length); /* 声明接口，读取用户FLASH状态返回。 */

/*! 
*  \brief  写用户FLASH状态返回
*  \param status 0失败，1成功
*/
void NotifyWriteFlash(uint8 status); /* 声明接口，写用户FLASH状态返回。 */

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
