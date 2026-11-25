#include "cmd_process.h"
#include "flag.h"
#include "coffemake.h"
#include "rtc_screen.h"
#include "elog.h"
#include <string.h>

/* HMI 分发运行在主循环。页面和控件决定动作，回调只更新参数或置流程标志，
 * 阀泵和液位控制由 100 ms 任务推进。屏幕 RTC 的通知走独立帧格式。 */
#define LOG_TAG     "cmd"

#define SCREEN_ID_WASH                     0x0002U
#define SCREEN_ID_COFFEE                   0x0004U   /* 当前按钮、文本和选择器分发使用的萃取页 ID。 */
#define CTRL_ID_WASH_START                0x0001U
#define BUTTON_STATE_RELEASE              0x00U
#define BUTTON_STATE_PRESS                0x01U

static void HandleWashScreenButton(uint16 control_id, uint8 state)    // 清洗页面按钮事件处理
{
    static uint8 wash_start_button_pressed = 0U;           // 记录启动按钮是否已经收到按下事件，用于等待抬起后再执行

    switch (control_id)
    {
    case CTRL_ID_WASH_START:
        if (state == BUTTON_STATE_PRESS)
        {
            wash_start_button_pressed = 1U;                // 收到按下事件先记状态，不在按下瞬间直接启动清洗
        }
        else if ((state == BUTTON_STATE_RELEASE) && (wash_start_button_pressed != 0U))
        {
            wash_start_button_pressed = 0U;                // 一次完整点击结束，清除临时状态
            FLAG_WASH_START = 1U;                          // 抬起瞬间置位清洗启动标志，由Wash_Task在100ms任务中启动流程
        }
        else
        {
            wash_start_button_pressed = 0U;                // 非法或不完整的按钮状态序列直接丢弃，避免误触发
        }
        break;

    default:
        break;                                             // 当前页面下其它按钮暂未绑定业务，后续在这里继续加case
    }
}

/* 萃取页面控件ID定义 */
#define CTRL_ID_COFFEE_VOLUME              0x0001U
#define CTRL_ID_COFFEE_TIME                0x0002U
#define CTRL_ID_COFFEE_DRAIN_MODE          0x0003U
#define CTRL_ID_COFFEE_START               0x0004U
#define CTRL_ID_COFFEE_DRAIN_WASTE         0x0005U     // 手动排废液按钮
#define CTRL_ID_COFFEE_DRAIN_BREW          0x0006U     // 手动排萃取液按钮

static void HandleCoffeeScreenButton(uint16 control_id, uint8 state)
{
    /* 三个流程按钮共用一次按下记录。记录只保存是否按下，未保存产生按下的控件 ID。 */
    static uint8 coffee_start_button_pressed = 0U;

    switch (control_id)
    {
    case CTRL_ID_COFFEE_START:
        if (state == BUTTON_STATE_PRESS)
        {
            coffee_start_button_pressed = 1U;
        }
        else if ((state == BUTTON_STATE_RELEASE) && (coffee_start_button_pressed != 0U))
        {
            coffee_start_button_pressed = 0U;
            FLAG_COFFEE_START = 1U;                              // 抬起瞬间置位萃取启动标志
        }
        else
        {
            coffee_start_button_pressed = 0U;
        }
        break;

    case CTRL_ID_COFFEE_DRAIN_WASTE:                             // 手动排废液
        if (state == BUTTON_STATE_PRESS)
        {
            coffee_start_button_pressed = 1U;
        }
        else if ((state == BUTTON_STATE_RELEASE) && (coffee_start_button_pressed != 0U))
        {
            coffee_start_button_pressed = 0U;
            FLAG_DRAIN_WASTE = 1U;                               // 手动触发放废液
        }
        else
        {
            coffee_start_button_pressed = 0U;
        }
        break;

    case CTRL_ID_COFFEE_DRAIN_BREW:                              // 手动排萃取液
        if (state == BUTTON_STATE_PRESS)
        {
            coffee_start_button_pressed = 1U;
        }
        else if ((state == BUTTON_STATE_RELEASE) && (coffee_start_button_pressed != 0U))
        {
            coffee_start_button_pressed = 0U;
            FLAG_DRAIN_BREW = 1U;                                // 手动触发放萃取液
        }
        else
        {
            coffee_start_button_pressed = 0U;
        }
        break;

    default:
        break;
    }
}

/*
 * HMI完整帧的最小固定长度（不含可变参数区param）：
 * 1) cmd_head      : 1字节
 * 2) cmd_type      : 1字节
 * 3) ctrl_msg      : 1字节
 * 4) screen_id     : 2字节
 * 5) control_id    : 2字节
 * 6) control_type  : 1字节
 * 7) cmd_tail      : 4字节
 * 合计：12字节
 */
#define HMI_FRAME_FIXED_LEN 12U                            //固定帧长度（字节数）

void ProcessMessage(PCTRL_MSG msg, uint16 size)            // 处理从串口接收的完整消息帧
{
    const uint8 *frame = (const uint8 *)msg;
    uint16 param_len = 0;
    /* 屏幕本地保存 RTC 后，只发自定义通知，主板收到后再主动读回实际时间。 */
    static const uint8 rtc_changed[] = {0xEE, 0xF0, 'R', 'T', 'C', 0x01};

    if (frame == 0)
        return;

    /* 读时回包按原始字节处理。它先于通用控件帧分发，因为帧内没有控件 ID。
     * EE F7 YY MM WK DD HH MM SS FF FC FF FF，时间字段从偏移 2 开始，共 7 字节。
     * 上层队列负责按帧头和帧尾提取完整帧，这里按总长度和命令识别类型。
     */
    if (size == 13U && frame[0] == 0xEEU && frame[1] == NOTIFY_READ_RTC)
    {
        RTC_Screen_OnRead(frame + 2);
        return;
    }
    /* 通知的固定内容之外还有 4 字节帧尾。长度吻合后再比较，避免将其它 F0 帧当成保存通知。 */
    if (size == sizeof(rtc_changed) + 4U &&
        memcmp(frame, rtc_changed, sizeof(rtc_changed)) == 0)
    {
        RTC_Screen_RequestSync();
        return;
    }

    /* EE B1 01 + 2 字节大端页面 ID + 4 字节帧尾，共 9 字节。
     * 此分支实际只检查最小长度和 ctrl_msg。0x01 也用于触摸按下，满足条件的帧会优先进入此分支。
     */
    if ((msg != 0) && (size >= 9U) && (msg->ctrl_msg == MSG_GET_CURRENT_SCREEN))
    {
        msg->screen_id  = PTR2U16((uint8 *)&msg->screen_id);
        NotifyScreen(msg->screen_id);
        return;
    }

    if (size < HMI_FRAME_FIXED_LEN)        // 通用消息至少需要固定字段和帧尾
    {
        return;
    }

    /* 固定字段之后为有效参数。数字类回调按各自的最小参数长度检查后再读取。 */
    param_len = (uint16)(size - HMI_FRAME_FIXED_LEN);

    /* 0x01 已由前面的页面号通知分支优先处理，这里再排除该值。 */
    if (msg->ctrl_msg == MSG_GET_CURRENT_SCREEN)
    {
        return;
    }

    // 屏幕发送的screen_id和control_id为大端序，STM32为小端，在入口处统一做字节序修正
    msg->screen_id  = PTR2U16((uint8 *)&msg->screen_id);
    msg->control_id = PTR2U16((uint8 *)&msg->control_id);

    /*
     * 分发策略：
     * 1) 第一级：按ctrl_msg判断这是“触摸通知/控件通知/系统通知”哪一类。
     * 2) 第二级：若为控件消息，则按control_type继续拆分。
     *
     * 这样做的好处是：协议扩展时只需新增case，不影响现有业务回调接口。
     */
    switch (msg->ctrl_msg)
    {
    case NOTIFY_TOUCH_PRESS:
    case NOTIFY_TOUCH_RELEASE:
        // 触摸坐标通知：param格式为X(2字节)+Y(2字节)。
        // press值沿用ctrl_msg：0x01按下，0x03松开。
        if (param_len >= 4U)
        {
            NotifyTouchXY(msg->ctrl_msg, PTR2U16(msg->param), PTR2U16(msg->param + 2));
        }
        break;

    case NOTIFY_CONTROL:                                    // 控件消息通知
    case MSG_GET_DATA:                                      // 控件数据通知
        switch (msg->control_type)
        {
        case kCtrlButton:
            // 按钮状态：param[1]，0=弹起，1=按下（大彩屏按钮上报格式：param[0]=触摸状态, param[1]=按钮状态）
            if (param_len >= 2U)
            {
                NotifyButton(msg->screen_id, msg->control_id, msg->param[1]);
            }
            break;

        case kCtrlText:
            /* 文本回调依赖发送方提供 NUL 结尾的参数。此分支直接传递指针，未核对终止符。 */
            NotifyText(msg->screen_id, msg->control_id, msg->param);
            break;

        case kCtrlProgress:
            // 进度条数值：4字节无符号整型（大端序）。
            if (param_len >= 4U)
            {
                NotifyProgress(msg->screen_id, msg->control_id, PTR2U32(msg->param));
            }
            break;

        case kCtrlSlider:
            // 滑动条数值：4字节无符号整型（大端序）。
            if (param_len >= 4U)
            {
                NotifySlider(msg->screen_id, msg->control_id, PTR2U32(msg->param));
            }
            break;

        case kCtrlMeter:
            // 仪表数值：4字节无符号整型（大端序）。
            if (param_len >= 4U)
            {
                NotifyMeter(msg->screen_id, msg->control_id, PTR2U32(msg->param));
            }
            break;

        case kCtrlMenu:
            // 菜单事件：param[0]=item索引，param[1]=状态（0松开，1按下）。
            if (param_len >= 2U)
            {
                NotifyMenu(msg->screen_id, msg->control_id, msg->param[0], msg->param[1]);
            }
            break;

        case kCtrlSelector:
            // 选择控件：param[0]=当前选项索引。
            if (param_len >= 1U)
            {
                NotifySelector(msg->screen_id, msg->control_id, msg->param[0]);
            }
            break;

        default:
            // 未使用控件类型：当前版本忽略，保留向前兼容。
            break;
        }
        break;

    case NOTIFY_TIMER:
        // 定时器控件超时通知：依赖screen_id与control_id定位具体定时器。
        NotifyTimer(msg->screen_id, msg->control_id);
        break;
  
    case NOTIFY_MENU:
        // 某些屏版本会以单独消息上报菜单事件，处理逻辑与kCtrlMenu一致。
        if (param_len >= 2U)
        {
            NotifyMenu(msg->screen_id, msg->control_id, msg->param[0], msg->param[1]);
        }
        break;

    case NOTIFY_READ_FLASH_OK:
        // 读用户FLASH成功，返回数据位于param区，长度为param_len。
        NotifyReadFlash(1U, msg->param, param_len);
        break;

    case NOTIFY_READ_FLASH_FAILD:
        // 读用户FLASH失败，通常不携带有效数据，仍透传status给业务层。
        NotifyReadFlash(0U, msg->param, param_len);
        break;

    case NOTIFY_WRITE_FLASH_OK:
        // 写用户FLASH成功。
        NotifyWriteFlash(1U);
        break;

    case NOTIFY_WRITE_FLASH_FAILD:
        // 写用户FLASH失败。
        NotifyWriteFlash(0U);
        break;

    default:
        // 未识别消息：默认忽略，避免因新协议字段导致旧固件异常。
        break;
    }
}

/*
 * NotifyButton、NotifyText 和 NotifySelector 已接入清洗、萃取或排液业务。
 * NotifyScreen 只记录日志。其余协议回调当前保留为空实现。
 */
/* 切页只记录调试日志。校时由主循环管理，屏幕保存时间后通过通知触发重读。 */
void NotifyScreen(uint16 screen_id)
{
    log_d("screen changed to %u", screen_id);
    (void)screen_id;
}
void NotifyTouchXY(uint8 press, uint16 x, uint16 y)         // 触摸坐标事件响应
{
    /* 触摸坐标保留为协议回调。当前固件从控件通知驱动业务。 */
    (void)press;
    (void)x;
    (void)y;
}

void NotifyButton(uint16 screen_id, uint16 control_id, uint8 state)
{
    switch (screen_id)
    {
    case SCREEN_ID_WASH:
        HandleWashScreenButton(control_id, state);         // 清洗页面的按钮统一在专用函数里处理
        return;

    case SCREEN_ID_COFFEE:
        HandleCoffeeScreenButton(control_id, state);       // 萃取页面的按钮统一在专用函数里处理
        return;

    /* 时间设置由屏幕本地处理。主板通过 RTC 保存通知同步，普通按钮信息走默认分支。 */
    default:
        break;                                             // 其它页面按钮暂未绑定业务，后续可继续新增页面处理函数
    }

    (void)screen_id;
    (void)control_id;
    (void)state;
}

void NotifyText(uint16 screen_id, uint16 control_id, uint8 *str)
{
    switch (screen_id)
    {
    case SCREEN_ID_COFFEE:
        Coffee_SetParam(control_id, str);                  // 萃取页面文本参数：液量/时间
        return;

    default:
        break;
    }

    (void)screen_id;
    (void)control_id;
    (void)str;
}

void NotifyProgress(uint16 screen_id, uint16 control_id, uint32 value)
{
    /* 当前未绑定进度条业务。 */
    (void)screen_id;
    (void)control_id;
    (void)value;
}

void NotifySlider(uint16 screen_id, uint16 control_id, uint32 value)
{
    /* 当前未绑定滑动条业务。 */
    (void)screen_id;
    (void)control_id;
    (void)value;
}

void NotifyMeter(uint16 screen_id, uint16 control_id, uint32 value)
{
    /* 当前未绑定仪表业务。 */
    (void)screen_id;
    (void)control_id;
    (void)value;
}

void NotifyMenu(uint16 screen_id, uint16 control_id, uint8 item, uint8 state)
{
    /* 当前未绑定菜单业务。 */
    (void)screen_id;
    (void)control_id;
    (void)item;
    (void)state;
}

void NotifySelector(uint16 screen_id, uint16 control_id, uint8 item)
{
    switch (screen_id)
    {
    case SCREEN_ID_COFFEE:
        if (control_id == CTRL_ID_COFFEE_DRAIN_MODE)
        {
            Coffee_SetDrainMode(item);                     // 萃取页面排水模式选择：0=自动, 1=手动
        }
        return;

    default:
        break;
    }

    (void)screen_id;
    (void)control_id;
    (void)item;
}

void NotifyTimer(uint16 screen_id, uint16 control_id)
{
    /* 当前未绑定屏幕定时器业务。 */
    (void)screen_id;
    (void)control_id;
}

void NotifyReadFlash(uint8 status, uint8 *_data, uint16 length)
{
    /* 屏幕用户 FLASH 回调当前为占位，与主板上的 CAT24C256 驱动独立。 */
    (void)status;
    (void)_data;
    (void)length;
}

void NotifyWriteFlash(uint8 status)
{
    (void)status;
}
