#include "cmd_process.h"
#include "flag.h"
#include "coffemake.h"
#include "rtc_storage.h"
#include "rtc_mcu.h"
#include "elog.h"
#include "delay.h"
#include <stdio.h>
#include <stdlib.h>

#define LOG_TAG     "cmd"

#define SCREEN_ID_WASH                     0x0002U
#define SCREEN_ID_COFFEE                   0x0004U   /* 萃取页 = 4 (之前错了) */
#define SCREEN_ID_RTC_SET                  0x000BU   /* 时间设置页面 */

/* 所有页面通用的时间文本控件 ID (显示 "HH:MM") */
#define CTRL_ID_TIME_TEXT                  0x0009U

/* 时间设置页面 控件ID */
#define CTRL_ID_RTC_YEAR                   0x0001U
#define CTRL_ID_RTC_MONTH                  0x0002U
#define CTRL_ID_RTC_DAY                    0x0003U
#define CTRL_ID_RTC_HOUR                   0x0004U
#define CTRL_ID_RTC_MINUTE                 0x0006U
#define CTRL_ID_RTC_BTN_OK                 0x0007U

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
// 固定帧结构长度：帧头(1) + 类型(1) + 子消息(1) + screen_id(2)
// + control_id(2) + control_type(1) + 帧尾(4) = 12字节。
#define HMI_FRAME_FIXED_LEN 12U                            //固定帧长度（字节数）

void ProcessMessage(PCTRL_MSG msg, uint16 size)            // 处理从串口接收的完整消息帧
{
    uint16 param_len = 0;

    /*
     * 基础健壮性检查：
     * 1) 空指针直接返回
     * 2) 画面通知帧只有 9 字节, 小于 12 字节的控件帧固定长度,
     *    必须在大小过滤之前先做前置特判
     */

    /* 画面ID变化通知: EE B1 01 screen_id(2) FF FC FF FF = 9 字节 */
    if ((msg != 0) && (size >= 9U) && (msg->ctrl_msg == MSG_GET_CURRENT_SCREEN))
    {
        msg->screen_id  = PTR2U16((uint8 *)&msg->screen_id);
        NotifyScreen(msg->screen_id);
        return;
    }

    /* NOTIFY_READ_RTC(0xF7): EE F7 [7 bytes] FF FC FF FF = 13 字节 */
    if ((msg != 0) && (size >= 13U) && (msg->ctrl_msg == NOTIFY_READ_RTC))
    {
        uint8 *rtc_ptr = ((uint8 *)msg) + 2;
        NotifyReadRTC(rtc_ptr[0], rtc_ptr[1], rtc_ptr[2],
                      rtc_ptr[3], rtc_ptr[4], rtc_ptr[5], rtc_ptr[6]);
        return;
    }

    if ((msg == 0) || (size < HMI_FRAME_FIXED_LEN))        // 为什么要这么写：先过滤空指针和短帧，避免后面访问越界
    {
        return;
    }

    // param_len表示可变参数区字节数，用于后续每类消息的长度校验。
    // 注意: NOTIFY_READ_RTC(0xF7) 帧不包含 screen_id/control_id/control_type,
    //       其结构为: EE F7 [7字节RTC数据] FF FC FF FF, msg结构体映射仅对控件消息有效
    param_len = (uint16)(size - HMI_FRAME_FIXED_LEN);

    /* MSG_GET_CURRENT_SCREEN 的字节序已在前置特判中处理, 此处不再重复 */
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
    switch (msg->ctrl_msg)                                 // 为什么要这么写：先按消息大类分发，结构清晰且易扩展
    {
    case NOTIFY_TOUCH_PRESS:
    case NOTIFY_TOUCH_RELEASE:
        // 触摸坐标通知：param格式为X(2字节)+Y(2字节)。
        // press值沿用ctrl_msg：0x01按下，0x03松开。
        if (param_len >= 4U)                               // 为什么要这么写：触摸坐标最少需要X(2)+Y(2)
        {
            NotifyTouchXY(msg->ctrl_msg, PTR2U16(msg->param), PTR2U16(msg->param + 2)); // 为什么要这么写：按协议从param拆出X/Y
        }
        break;                                              // 为什么要这么写：触摸消息处理完成后退出一级分发

    case NOTIFY_CONTROL:                                    // 控件消息通知
    case MSG_GET_DATA:                                      // 控件数据通知
        switch (msg->control_type)                         // 为什么要这么写：控件消息再按控件类型二级分发
        {
        case kCtrlButton:
            // 按钮状态：param[1]，0=弹起，1=按下（大彩屏按钮上报格式：param[0]=触摸状态, param[1]=按钮状态）
            if (param_len >= 2U)                           // 为什么要这么写：按钮状态需要2字节(param[0]+param[1])
            {
                NotifyButton(msg->screen_id, msg->control_id, msg->param[1]); // 为什么要这么写：param[1]才是按钮真实状态
            }
            break;                                          // 为什么要这么写：当前控件类型已处理完毕

        case kCtrlText:
            // 文本内容：param起始地址为字符串首地址（通常以\0结束）。
            NotifyText(msg->screen_id, msg->control_id, msg->param); // 为什么要这么写：文本是变长数据，直接传首地址最灵活
            break;                                          // 为什么要这么写：文本分支结束

        case kCtrlProgress:
            // 进度条数值：4字节无符号整型（大端序）。
            if (param_len >= 4U)                           // 为什么要这么写：uint32数值必须保证4字节可读
            {
                NotifyProgress(msg->screen_id, msg->control_id, PTR2U32(msg->param)); // 为什么要这么写：用统一宏做大端转本地整数
            }
            break;                                          // 为什么要这么写：进度条分支结束

        case kCtrlSlider:
            // 滑动条数值：4字节无符号整型（大端序）。
            if (param_len >= 4U)                           // 为什么要这么写：防止参数不足时读取越界
            {
                NotifySlider(msg->screen_id, msg->control_id, PTR2U32(msg->param)); // 为什么要这么写：滑动条协议值同样是4字节整型
            }
            break;                                          // 为什么要这么写：滑动条分支结束

        case kCtrlMeter:
            // 仪表数值：4字节无符号整型（大端序）。
            if (param_len >= 4U)                           // 为什么要这么写：保证读取完整数值
            {
                NotifyMeter(msg->screen_id, msg->control_id, PTR2U32(msg->param)); // 为什么要这么写：仪表值直接转交业务层处理
            }
            break;                                          // 为什么要这么写：仪表分支结束

        case kCtrlMenu:
            // 菜单事件：param[0]=item索引，param[1]=状态（0松开，1按下）。
            if (param_len >= 2U)                           // 为什么要这么写：菜单至少需要item和state两个字节
            {
                NotifyMenu(msg->screen_id, msg->control_id, msg->param[0], msg->param[1]); // 为什么要这么写：菜单逻辑需要索引和状态同时判断
            }
            break;                                          // 为什么要这么写：菜单分支结束

        case kCtrlSelector:
            // 选择控件：param[0]=当前选项索引。
            if (param_len >= 1U)                           // 为什么要这么写：选择控件值只需1字节
            {
                NotifySelector(msg->screen_id, msg->control_id, msg->param[0]); // 为什么要这么写：把选项索引传给业务逻辑
            }
            break;                                          // 为什么要这么写：选择控件分支结束

        default:
            // 未使用控件类型：当前版本忽略，保留向前兼容。
            break;                                          // 为什么要这么写：未知控件不处理，避免误动作
        }
        break;                                              // 为什么要这么写：控件消息处理完成，退出一级分发

    case NOTIFY_TIMER:
        // 定时器控件超时通知：依赖screen_id与control_id定位具体定时器。
        NotifyTimer(msg->screen_id, msg->control_id);      // 为什么要这么写：把超时事件转给业务层决定动作
        break;                                              // 为什么要这么写：定时器分支结束
  
    case NOTIFY_MENU:
        // 某些屏版本会以单独消息上报菜单事件，处理逻辑与kCtrlMenu一致。
        if (param_len >= 2U)                               // 为什么要这么写：同样要保证item/state都有值
        {
            NotifyMenu(msg->screen_id, msg->control_id, msg->param[0], msg->param[1]); // 为什么要这么写：复用统一菜单通知接口
        }
        break;                                              // 为什么要这么写：菜单独立通知分支结束

    case NOTIFY_READ_FLASH_OK:
        // 读用户FLASH成功，返回数据位于param区，长度为param_len。
        NotifyReadFlash(1U, msg->param, param_len);        // 为什么要这么写：用status=1显式区分成功路径
        break;                                              // 为什么要这么写：读FLASH成功分支结束

    case NOTIFY_READ_FLASH_FAILD:
        // 读用户FLASH失败，通常不携带有效数据，仍透传status给业务层。
        NotifyReadFlash(0U, msg->param, param_len);        // 为什么要这么写：失败也走同一接口，业务层更好统一处理
        break;                                              // 为什么要这么写：读FLASH失败分支结束

    case NOTIFY_WRITE_FLASH_OK:
        // 写用户FLASH成功。
        NotifyWriteFlash(1U);                              // 为什么要这么写：成功状态单独上报，便于提示用户
        break;                                              // 为什么要这么写：写FLASH成功分支结束

    case NOTIFY_WRITE_FLASH_FAILD:
        // 写用户FLASH失败。
        NotifyWriteFlash(0U);                              // 为什么要这么写：失败状态必须可观测，便于重试策略
        break;                                              // 为什么要这么写：写FLASH失败分支结束

    default:
        // 未识别消息：默认忽略，避免因新协议字段导致旧固件异常。
        break;                                              // 为什么要这么写：未知消息不处理，保持系统健壮
    }
}

/*
 * 以下Notify函数是“业务对接层”的标准入口。
 * 当前给出空实现（仅抑制未使用参数告警），你可按项目需求填入控制逻辑。
 *
 * 推荐实践：
 * 1) 在NotifyButton里按screen_id + control_id做按钮路由。
 * 2) 在NotifyText里把字符串转成参数并做范围检查。
 * 3) 复杂动作只置标志位，在主任务中执行，避免回调里阻塞。
 */
/* 当前所在页面 ID (由 NotifyScreen 维护, main.c 中每秒刷新时间使用) */
uint16_t g_current_screen = 0;

/* RTC 时间设置页文本缓存 */
uint8_t  _rtc_collected = 0;
uint16_t _rtc_year      = 2000;
uint8_t  _rtc_month     = 1;
uint8_t  _rtc_day       = 1;
uint8_t  _rtc_hour      = 0;
uint8_t  _rtc_min       = 0;

void NotifyScreen(uint16 screen_id)                         // 画面ID变化通知
{
    g_current_screen = screen_id;
    log_d("screen changed to %u", screen_id);

    switch (screen_id)
    {
    case SCREEN_ID_RTC_SET:
        {
            RTC_Time_t t;
            char buf[6];
            RTC_MCU_GetTime(&t);
            _rtc_collected = 0x1F;
            _rtc_year  = t.year;
            _rtc_month = t.month;
            _rtc_day   = t.day;
            _rtc_hour  = t.hour;
            _rtc_min   = t.minute;
            snprintf(buf, sizeof(buf), "%u", t.year);   HMI_SendTextFrame(SCREEN_ID_RTC_SET, CTRL_ID_RTC_YEAR,   buf);
            snprintf(buf, sizeof(buf), "%u", t.month);  HMI_SendTextFrame(SCREEN_ID_RTC_SET, CTRL_ID_RTC_MONTH,  buf);
            snprintf(buf, sizeof(buf), "%u", t.day);    HMI_SendTextFrame(SCREEN_ID_RTC_SET, CTRL_ID_RTC_DAY,    buf);
            snprintf(buf, sizeof(buf), "%u", t.hour);   HMI_SendTextFrame(SCREEN_ID_RTC_SET, CTRL_ID_RTC_HOUR,   buf);
            snprintf(buf, sizeof(buf), "%u", t.minute); HMI_SendTextFrame(SCREEN_ID_RTC_SET, CTRL_ID_RTC_MINUTE, buf);
        }
        return;

    default:
        {
            RTC_Time_t t;
            char buf[8];
            RTC_MCU_GetTime(&t);
            RTC_MCU_FormatHM(&t, buf, sizeof(buf));
            HMI_SendTextFrame(screen_id, CTRL_ID_TIME_TEXT, buf);
        }
        break;
    }

    (void)screen_id;
}

void NotifyTouchXY(uint8 press, uint16 x, uint16 y)         // 触摸坐标事件响应
{
    // 建议：根据press区分按下/松开，结合坐标做调试或手势逻辑。
    (void)press;                                            // 为什么要这么写：占位实现，后续按需接入触摸业务
    (void)x;                                                // 为什么要这么写：占位实现，避免编译器告警
    (void)y;                                                // 为什么要这么写：占位实现，避免编译器告警
}

void NotifyButton(uint16 screen_id, uint16 control_id, uint8 state)
{
    /* 时间设置页 —— 确定按钮: 直接用缓存值 */
    if (screen_id == SCREEN_ID_RTC_SET && control_id == CTRL_ID_RTC_BTN_OK && state == BUTTON_STATE_PRESS)
    {
        RTC_Time_t t;

        if (_rtc_collected != 0x1F)
        {
            log_w("RTC text not all collected (0x%02X), ignored", _rtc_collected);
            return;
        }

        _rtc_collected = 0;

        t.year   = _rtc_year;
        t.month  = _rtc_month;
        t.day    = _rtc_day;
        t.hour   = _rtc_hour;
        t.minute = _rtc_min;
        t.second = 0;
        t.week   = RTC_CalcWeek(t.year, t.month, t.day);

        if (RTC_IsValid(&t))
        {
            RTC_MCU_SetTime(&t);
            RTC_SaveToEEPROM(&t);
            log_i("RTC set: %u-%02u-%02u %02u:%02u:%02u",
                  t.year, t.month, t.day, t.hour, t.minute, t.second);
        }
        else
        {
            log_w("RTC set invalid: %u-%02u-%02u %02u:%02u",
                  t.year, t.month, t.day, t.hour, t.minute);
        }
        return;
    }
    switch (screen_id)
    {
    case SCREEN_ID_WASH:
        HandleWashScreenButton(control_id, state);         // 清洗页面的按钮统一在专用函数里处理
        return;

    case SCREEN_ID_COFFEE:
        HandleCoffeeScreenButton(control_id, state);       // 萃取页面的按钮统一在专用函数里处理
        return;

    /* 时间设置页 —— 确定按钮 (screen_id=0x000B, control_id=0x0007) */
    case SCREEN_ID_RTC_SET:
        if (control_id == CTRL_ID_RTC_BTN_OK && state == BUTTON_STATE_PRESS)
        {
            log_i("RTC set OK button pressed, reading 5 texts...");
            /* 按钮按下后屏幕自动切页, MCU 依次读取 5 个文本控件的值 */
            GetControlValue(SCREEN_ID_RTC_SET, CTRL_ID_RTC_YEAR);
            GetControlValue(SCREEN_ID_RTC_SET, CTRL_ID_RTC_MONTH);
            GetControlValue(SCREEN_ID_RTC_SET, CTRL_ID_RTC_DAY);
            GetControlValue(SCREEN_ID_RTC_SET, CTRL_ID_RTC_HOUR);
            GetControlValue(SCREEN_ID_RTC_SET, CTRL_ID_RTC_MINUTE);
        }
        return;

    default:
        break;                                             // 其它页面按钮暂未绑定业务，后续可继续新增页面处理函数
    }

    (void)screen_id;                                        // 为什么要这么写：当前未绑定其它页面按钮业务，先保留接口
    (void)control_id;                                       // 为什么要这么写：当前未绑定其它页面按钮业务，先保留接口
    (void)state;                                            // 为什么要这么写：当前未绑定其它页面按钮业务，先保留接口
}

void NotifyText(uint16 screen_id, uint16 control_id, uint8 *str)
{
    /* 时间设置页 —— 缓存年/月/日/时/分, 按钮按下时统一写入 */
    if (screen_id == SCREEN_ID_RTC_SET)
    {
        extern uint8_t  _rtc_collected;
        extern uint16_t _rtc_year;
        extern uint8_t  _rtc_month, _rtc_day, _rtc_hour, _rtc_min;
        uint16_t val;

        val = (uint16_t)atoi((const char *)str);

        switch (control_id)
        {
        case CTRL_ID_RTC_YEAR:   _rtc_year  = val; _rtc_collected |= 0x01; break;
        case CTRL_ID_RTC_MONTH:  _rtc_month = (uint8_t)val; _rtc_collected |= 0x02; break;
        case CTRL_ID_RTC_DAY:    _rtc_day   = (uint8_t)val; _rtc_collected |= 0x04; break;
        case CTRL_ID_RTC_HOUR:   _rtc_hour  = (uint8_t)val; _rtc_collected |= 0x08; break;
        case CTRL_ID_RTC_MINUTE: _rtc_min   = (uint8_t)val; _rtc_collected |= 0x10; break;
        default: return;
        }

        log_i("RTC cache: collected=0x%02X y=%u m=%u d=%u h=%u min=%u",
              _rtc_collected, _rtc_year, _rtc_month, _rtc_day, _rtc_hour, _rtc_min);
        return;
    }

    switch (screen_id)
    {
    case SCREEN_ID_COFFEE:
        Coffee_SetParam(control_id, str);                  // 萃取页面文本参数：液量/时间
        return;

    default:
        break;
    }

    // 建议：把str解析为数值或命令前先做长度与格式检查。
    (void)screen_id;                                        // 为什么要这么写：占位接口，业务后续填写
    (void)control_id;                                       // 为什么要这么写：占位接口，业务后续填写
    (void)str;                                              // 为什么要这么写：占位接口，业务后续填写
}

void NotifyProgress(uint16 screen_id, uint16 control_id, uint32 value)
{
    // 建议：若需要双向联动，可在这里把屏侧进度同步到本地变量。
    (void)screen_id;                                        // 为什么要这么写：暂未使用参数，保留函数结构
    (void)control_id;                                       // 为什么要这么写：暂未使用参数，保留函数结构
    (void)value;                                            // 为什么要这么写：暂未使用参数，保留函数结构
}

void NotifySlider(uint16 screen_id, uint16 control_id, uint32 value)
{
    // 建议：用于参数调节场景时，增加上下限钳制和生效条件。
    (void)screen_id;                                        // 为什么要这么写：暂未使用参数，避免警告
    (void)control_id;                                       // 为什么要这么写：暂未使用参数，避免警告
    (void)value;                                            // 为什么要这么写：暂未使用参数，避免警告
}

void NotifyMeter(uint16 screen_id, uint16 control_id, uint32 value)
{
    // 建议：通常用于显示回读，必要时可用于闭环校验。
    (void)screen_id;                                        // 为什么要这么写：暂未接业务，先保持模板接口
    (void)control_id;                                       // 为什么要这么写：暂未接业务，先保持模板接口
    (void)value;                                            // 为什么要这么写：暂未接业务，先保持模板接口
}

void NotifyMenu(uint16 screen_id, uint16 control_id, uint8 item, uint8 state)
{
    // 建议：item用于区分菜单项，state用于区分按下/松开动作。
    (void)screen_id;                                        // 为什么要这么写：模板阶段先抑制未用参数
    (void)control_id;                                       // 为什么要这么写：模板阶段先抑制未用参数
    (void)item;                                             // 为什么要这么写：模板阶段先抑制未用参数
    (void)state;                                            // 为什么要这么写：模板阶段先抑制未用参数
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

    // 建议：选择框变化时，可在此切换本地配置档位。
    (void)screen_id;                                        // 为什么要这么写：占位接口，后续按业务填充
    (void)control_id;                                       // 为什么要这么写：占位接口，后续按业务填充
    (void)item;                                             // 为什么要这么写：占位接口，后续按业务填充
}

void NotifyTimer(uint16 screen_id, uint16 control_id)
{
    // 建议：仅置位业务标志，由主循环处理耗时任务。
    (void)screen_id;                                        // 为什么要这么写：当前未接定时器业务，先保持编译通过
    (void)control_id;                                       // 为什么要这么写：当前未接定时器业务，先保持编译通过
}

void NotifyReadFlash(uint8 status, uint8 *_data, uint16 length)
{
    // 建议：status=1时解析_data，status=0时做重试或错误上报。
    (void)status;                                           // 为什么要这么写：模板阶段先保留状态参数
    (void)_data;                                            // 为什么要这么写：模板阶段先保留数据指针
    (void)length;                                           // 为什么要这么写：模板阶段先保留长度参数
}

void NotifyWriteFlash(uint8 status)
{
    // 建议：根据status提示保存成功/失败，并决定是否重发。
    (void)status;                                           // 为什么要这么写：当前仅保留接口，待业务实现
}

void NotifyReadRTC(uint8 year, uint8 month, uint8 week, uint8 day, uint8 hour, uint8 minute, uint8 second)
{
    (void)year; (void)month; (void)week; (void)day;
    (void)hour; (void)minute; (void)second;
}

/* 分钟变化时刷新当前页面时间 (由 main 循环中 FLAG_1S 调用) */
void RTC_RefreshIfMinuteChanged(void)
{
    static uint8_t last_min = 0xFF;
    RTC_Time_t t;

    if (g_current_screen == SCREEN_ID_RTC_SET) return;
    RTC_MCU_GetTime(&t);
    if (t.minute != last_min)
    {
        char buf[8];
        last_min = t.minute;
        RTC_MCU_FormatHM(&t, buf, sizeof(buf));
        HMI_SendTextFrame(g_current_screen, CTRL_ID_TIME_TEXT, buf);
        RTC_SaveToEEPROM(&t);    /* 每分钟同步到 EEPROM */
    }
}

/* 一次性 DMA 发送 SetTextValue 帧 (绕过 SendChar 逐字节阻塞) */
void HMI_SendTextFrame(uint16_t screen_id, uint16_t control_id, const char *str)
{
    uint8_t frame[64];
    uint8_t idx = 0;
    uint8_t i;

    frame[idx++] = 0xEE;
    frame[idx++] = 0xB1;
    frame[idx++] = 0x10;
    frame[idx++] = (uint8_t)(screen_id >> 8);
    frame[idx++] = (uint8_t)(screen_id);
    frame[idx++] = (uint8_t)(control_id >> 8);
    frame[idx++] = (uint8_t)(control_id);
    for (i = 0; str[i] != '\0'; i++)
        frame[idx++] = (uint8_t)str[i];
    frame[idx++] = 0xFF;
    frame[idx++] = 0xFC;
    frame[idx++] = 0xFF;
    frame[idx++] = 0xFF;

    USART2_DMA_SendData(frame, idx);
}
