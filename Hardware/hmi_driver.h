/*! 
 *  \file hmi_driver.h
 *  \brief 串口屏驱动文件
 *  \version 1.0
 *  \date 2012-2015
 *  \copyright 广州大彩光电科技有限公司
 */

#ifndef _HMI_DRIVER_ /* 仅在尚未定义 _HMI_DRIVER_时编译下方代码。 */
#define _HMI_DRIVER_ /* 定义 _HMI_DRIVER_，供头文件防重包含或后续条件编译检查。 */

#define FIRMWARE_VER 917   /*!< 编译期屏幕协议版本，决定部分 API 的命令变体。 */
#define CRC16_ENABLE 0         /*!< 当前工程与屏幕使用无 CRC 帧。启用时须同时核对 VisualTFT 和接收分发。 */
#define CMD_MAX_SIZE 64        /*!< 单帧接收缓冲区大小，含帧头与帧尾。 */
#define QUEUE_MAX_SIZE 128   /*!< 字节环形队列槽位数，实际容量少 1 字节。 */

#define SD_FILE_EN 0 /* 定义 SD_FILE_EN 的约定值 0，使对应判断或封装统一引用。 */

#include "USARTDMA.h" /* 取得日志与触摸屏串口的 DMA 收发接口。 */
#include <stdint.h> /* 使用位宽明确的整数类型，匹配寄存器宽度及协议字段。 */

typedef uint8_t uint8; /* 声明类型别名 uint8，供其后的接口和变量统一使用。 */
typedef uint16_t uint16; /* 声明类型别名 uint16，供其后的接口和变量统一使用。 */
typedef uint32_t uint32; /* 声明类型别名 uint32，供其后的接口和变量统一使用。 */
typedef int16_t int16; /* 声明类型别名 int16，供其后的接口和变量统一使用。 */
typedef uint8_t uchar; /* 声明类型别名 uchar，供其后的接口和变量统一使用。 */

/*! 
 *  \brief  检查数据是否符合CRC16校验
 *  \param buffer 待校验的数据，末尾存储CRC16
 *  \param n 数据长度，包含CRC16
 *  \return 校验通过返回1，否则返回0
 */
uint16 CheckCRC16(uint8 *buffer,uint16 n); /* 声明接口，比较帧内的 CRC 与按有效字节重算的 CRC。 */

/*! 
 *  \brief  厂商接口保留的空循环忙等，耗时取决于编译优化和主频。
 *  \param  n 外层循环次数
 */
void DelayMS(unsigned int n); /* 声明接口，厂商接口保留的空循环忙等，耗时取决于编译优化和主频。 */

/*! 
 *  \brief  锁定设备配置，锁定之后需要解锁，才能修改波特率、触摸屏、蜂鸣器工作方式
 */
void LockDeviceConfig(void); /* 声明接口，锁定设备配置，锁定之后需要解锁，才能修改波特率、触摸屏、蜂鸣器工作方式。 */

/*! 
 *  \brief  解锁设备配置
 */
void UnlockDeviceConfig(void); /* 声明接口，解锁设备配置。 */

/*! 
 *  \brief     修改串口屏的波特率
 *  \details  波特率选项范围[0~14]，对应实际波特率
                   {1200,2400,4800,9600,19200,38400,57600,115200,1000000,2000000,218750,437500,875000,921800,2500000}
 *  \param  option 波特率选项
 */
void SetCommBps(uint8 option); /* 声明接口，修改串口屏的波特率。 */

/*! 
 *  \brief  发送握手命令
 */
void SetHandShake(void); /* 声明接口，发送握手命令。 */

/*! 
 *  \brief  设置前景色
 *  \param  color 前景色
 */
void SetFcolor(uint16 color); /* 声明接口，设置前景色。 */

/*! 
 *  \brief  设置背景色
 *  \param  color 背景色
 */
void SetBcolor(uint16 color); /* 声明接口，设置背景色。 */

/*! 
 *  \brief  清除画面
 */
void GUI_CleanScreen(void); /* 声明接口，清除画面。 */

/*! 
 *  \brief  设置文字间隔
 *  \param  x_w 横向间隔
  *  \param  y_w 纵向间隔
 */
void SetTextSpace(uint8 x_w, uint8 y_w); /* 声明接口，设置文字间隔。 */

/*! 
 *  \brief  设置文字显示限制
 *  \param  enable 是否启用限制
 *  \param  width 宽度
 *  \param  height 高度
 */
void SetFont_Region(uint8 enable,uint16 width,uint16 height ); /* 声明接口，设置文字显示限制。 */

/*! 
 *  \brief  设置过滤色
 *  \param  fillcolor_dwon 颜色下界
 *  \param  fillcolor_up 颜色上界
 */
void SetFilterColor(uint16 fillcolor_dwon, uint16 fillcolor_up); /* 声明接口，设置过滤色。 */

/*! 
 *  \brief  在指定坐标绘制文本。
 *  \param  x 位置X坐标
 *  \param  y 位置Y坐标
 *  \param  back 背景绘制参数，按一个字节发送
 *  \param  font 字体
 *  \param  strings 字符串内容
 */
void DisText(uint16 x, uint16 y,uint8 back,uint8 font,uchar *strings ); /* 声明接口，在指定坐标绘制文本。 */

/*! 
 *  \brief    显示光标
 *  \param  enable 是否显示
 *  \param  x 位置X坐标
 *  \param  y 位置Y坐标
 *  \param  width 宽度
 *  \param  height 高度
 */
void DisCursor(uint8 enable,uint16 x, uint16 y,uint8 width,uint8 height ); /* 声明接口，显示光标。 */

/*! 
 *  \brief      显示全屏图片
 *  \param  image_id 图片索引
 *  \param  masken 是否启用透明掩码
 */
void DisFull_Image(uint16 image_id,uint8 masken); /* 声明接口，显示全屏图片。 */

/*! 
 *  \brief      指定位置显示图片
 *  \param  x 位置X坐标
 *  \param  y 位置Y坐标
 *  \param  image_id 图片索引
 *  \param  masken 是否启用透明掩码
 */
void DisArea_Image(uint16 x,uint16 y,uint16 image_id,uint8 masken); /* 声明接口，指定位置显示图片。 */

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
void DisCut_Image(uint16 x,uint16 y,uint16 image_id,uint16 image_x,uint16 image_y, /* 声明接口，显示裁剪图片。 */
                   uint16 image_l, uint16 image_w,uint8 masken); /* 定义图片 l字段，供这组配置或数据结构使用。 */

/*! 
 *  \brief      显示GIF动画
 *  \param  x 位置X坐标
 *  \param  y 位置Y坐标
 *  \param  flashimage_id 图片索引
 *  \param  enable 是否显示
 *  \param  playnum 播放次数
 */
void DisFlashImage(uint16 x,uint16 y,uint16 flashimage_id,uint8 enable,uint8 playnum); /* 声明接口，显示GIF动画。 */

/*! 
 *  \brief      画点
 *  \param  x 位置X坐标
 *  \param  y 位置Y坐标
 */
void GUI_Dot(uint16 x,uint16 y); /* 声明接口，画点。 */

/*! 
 *  \brief      画线
 *  \param  x0 起始位置X坐标
 *  \param  y0 起始位置Y坐标
 *  \param  x1 结束位置X坐标
 *  \param  y1 结束位置Y坐标
 */
void GUI_Line(uint16 x0, uint16 y0, uint16 x1, uint16 y1); /* 声明接口，画线。 */

/*! 
 *  \brief      画折线
 *  \param  mode 模式
 *  \param  dot 数据点
 *  \param  dot_cnt 点数
 */
void GUI_ConDots(uint8 mode,uint16 *dot,uint16 dot_cnt); /* 声明接口，画折线。 */

/*! 
 *  \brief      画空心圆
 *  \param  x0 圆心位置X坐标
 *  \param  y0 圆心位置Y坐标
 *  \param  r 半径
 */
void GUI_Circle(uint16 x0, uint16 y0, uint16 r); /* 声明接口，画空心圆。 */

/*! 
 *  \brief      画实心圆
 *  \param  x0 圆心位置X坐标
 *  \param  y0 圆心位置Y坐标
 *  \param  r 半径
 */
void GUI_CircleFill(uint16 x0, uint16 y0, uint16 r); /* 声明接口，画实心圆。 */

/*! 
 *  \brief      画弧线
 *  \param  x 圆心 X 坐标
 *  \param  y 圆心 Y 坐标
 *  \param  r 半径
 *  \param  sa 起始角度
 *  \param  ea 终止角度
 */
void GUI_Arc(uint16 x,uint16 y, uint16 r,uint16 sa, uint16 ea); /* 声明接口，画弧线。 */

/*! 
 *  \brief      画空心矩形
 *  \param  x0 起始位置X坐标
 *  \param  y0 起始位置Y坐标
 *  \param  x1 结束位置X坐标
 *  \param  y1 结束位置Y坐标
 */
void GUI_Rectangle(uint16 x0, uint16 y0, uint16 x1,uint16 y1 ); /* 声明接口，画空心矩形。 */

/*! 
 *  \brief      画实心矩形
 *  \param  x0 起始位置X坐标
 *  \param  y0 起始位置Y坐标
 *  \param  x1 结束位置X坐标
 *  \param  y1 结束位置Y坐标
 */
void GUI_RectangleFill(uint16 x0, uint16 y0, uint16 x1,uint16 y1 ); /* 声明接口，画实心矩形。 */

/*! 
 *  \brief      画空心椭圆
 *  \param  x0 起始位置X坐标
 *  \param  y0 起始位置Y坐标
 *  \param  x1 结束位置X坐标
 *  \param  y1 结束位置Y坐标
 */
void GUI_Ellipse (uint16 x0, uint16 y0, uint16 x1,uint16 y1 ); /* 声明接口，画空心椭圆。 */

/*! 
 *  \brief      画实心椭圆
 *  \param  x0 起始位置X坐标
 *  \param  y0 起始位置Y坐标
 *  \param  x1 结束位置X坐标
 *  \param  y1 结束位置Y坐标
 */
void GUI_EllipseFill (uint16 x0, uint16 y0, uint16 x1,uint16 y1 ); /* 声明接口，画实心椭圆。 */

/*! 
 *  \brief      发送屏幕背光亮度设置指令 0x60。
 *  \param  light_level 单字节亮度参数，原样发送
 */
void SetBackLight(uint8 light_level); /* 声明接口，发送屏幕背光亮度设置指令 0x60。 */

/*! 
 *  \brief   蜂鸣器设置
 *  \param  time 发送给屏幕的单字节蜂鸣持续时间参数
 */
void SetBuzzer(uint8 time); /* 声明接口，蜂鸣器设置。 */

/*! 
 *  \brief   触摸屏设置
 *  \param enbale 触摸使能（参数名沿用兼容接口）
 *  \param beep_on 触摸蜂鸣器
 *  \param work_mode 触摸工作模式：0按下就上传，1松开才上传，2不断上传坐标值，3按下和松开均上传数据
 *  \param press_calibration 连续点击触摸屏20下校准触摸屏：0禁用，1启用
 */
void SetTouchPaneOption(uint8 enbale,uint8 beep_on,uint8 work_mode,uint8 press_calibration); /* 声明接口，触摸屏设置。 */

/*! 
 *  \brief   校准触摸屏
 */
void	CalibrateTouchPane(void); /* 声明接口，校准触摸屏。 */

/*! 
 *  \brief  触摸屏测试
 */
void TestTouchPane(void); /* 声明接口，触摸屏测试。 */

/*! 
 *  \brief      设置当前写入图层
 *  \details  一般用于实现双缓存效果(绘图时避免闪烁)：
 *  \details  uint8 layer = 0;
 *  \details  WriteLayer(layer);    //设置写入层
 *  \details  ClearLayer(layer);    //使图层变透明
 *  \details  //添加一系列绘图指令
 *  \details  //DisText(100,100,0,4,"hello hmi!!!");
 *  \details  DisplyLayer(layer);  //切换显示层
 *  \details  layer = (layer+1)%2;  //双缓存切换
 *  \see DisplyLayer
 *  \see ClearLayer
 *  \param  layer 图层编号
 */
void WriteLayer(uint8 layer); /* 声明接口，设置当前写入图层。 */

/*! 
 *  \brief      设置当前显示图层
 *  \param  layer 图层编号
 */
void DisplyLayer(uint8 layer); /* 声明接口，设置当前显示图层。 */

/*! 
 *  \brief      清除图层，使图层变成透明
 *  \param  layer 图层编号
 */
void ClearLayer(uint8 layer); /* 声明接口，清除图层，使图层变成透明。 */

/*! 
 *  \brief  写数据到串口屏用户存储区
 *  \param  startAddress 起始地址
 *  \param  length 字节数
 *  \param  _data 待写入的数据
 */
void WriteUserFlash(uint32 startAddress,uint16 length,uint8 *_data); /* 声明接口，写数据到串口屏用户存储区。 */

/*! 
 *  \brief  从串口屏用户存储区读取数据
 *  \param  startAddress 起始地址
 *  \param  length 字节数
 */
void ReadUserFlash(uint32 startAddress,uint16 length); /* 声明接口，从串口屏用户存储区读取数据。 */

/*! 
 *  \brief      拷贝图层
 *  \param  src_layer 原始图层
 *  \param  dest_layer 目标图层
 */
void CopyLayer(uint8 src_layer,uint8 dest_layer); /* 声明接口，拷贝图层。 */

/*! 
 *  \brief      设置当前画面
 *  \param  screen_id 画面ID
 */
void SetScreen(uint16 screen_id); /* 声明接口，设置当前画面。 */

/*! 
 *  \brief      获取当前画面
 */
void GetScreen(void); /* 声明接口，获取当前画面。 */

/*! 
 *  \brief     禁用\启用画面更新
 *  \details 禁用\启用一般成对使用，用于避免闪烁、提高刷新速度
 *  \details 用法：
 *	\details SetScreenUpdateEnable(0);//禁止更新
 *	\details 一系列更新画面的指令
 *	\details SetScreenUpdateEnable(1);//立即更新
 *  \param  enable 0禁用，1启用
 */
void SetScreenUpdateEnable(uint8 enable); /* 声明接口，禁用\启用画面更新。 */

/*! 
 *  \brief     设置控件输入焦点
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  focus 是否具有输入焦点
 */
void SetControlFocus(uint16 screen_id,uint16 control_id,uint8 focus); /* 声明接口，设置控件输入焦点。 */

/*! 
 *  \brief     显示\隐藏控件
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  visible 是否显示
 */
void SetControlVisiable(uint16 screen_id,uint16 control_id,uint8 visible); /* 声明接口，显示\隐藏控件。 */

/*! 
 *  \brief     设置触摸控件使能
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  enable 控件是否使能
 */
void SetControlEnable(uint16 screen_id,uint16 control_id,uint8 enable); /* 声明接口，设置触摸控件使能。 */

/*! 
 *  \brief     获取控件值
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 */
void GetControlValue(uint16 screen_id,uint16 control_id); /* 声明接口，获取控件值。 */

/*! 
 *  \brief     设置按钮状态
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  value 按钮状态
 */
void SetButtonValue(uint16 screen_id,uint16 control_id,uchar value); /* 声明接口，设置按钮状态。 */

/*! 
 *  \brief     设置文本值
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  str 文本值
 */
void SetTextValue(uint16 screen_id,uint16 control_id,uchar *str); /* 声明接口，设置文本值。 */

#if FIRMWARE_VER>=908 /* 仅在「FIRMWARE_VER>=908」 成立时编译下方代码。 */

/*! 
 *  \brief     设置文本为整数值，要求FIRMWARE_VER>=908
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  value 文本数值
 *  \param  sign 0-无符号，1-有符号
 *  \param  fill_zero 数字位数，不足时左侧补零
 */
void SetTextInt32(uint16 screen_id,uint16 control_id,uint32 value,uint8 sign,uint8 fill_zero); /* 声明接口，设置文本为整数值，要求FIRMWARE_VER>=908。 */

/*! 
 *  \brief     设置文本单精度浮点值，要求FIRMWARE_VER>=908
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  value 文本数值
 *  \param  precision 小数位数
 *  \param  show_zeros 为1时，显示末尾0
 */
void SetTextFloat(uint16 screen_id,uint16 control_id,float value,uint8 precision,uint8 show_zeros); /* 声明接口，设置文本单精度浮点值，要求FIRMWARE_VER>=908。 */

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

/*! 
 *  \brief      设置进度值
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  value 数值
 */
void SetProgressValue(uint16 screen_id,uint16 control_id,uint32 value); /* 声明接口，设置进度值。 */

/*! 
 *  \brief     设置仪表值
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  value 数值
 */
void SetMeterValue(uint16 screen_id,uint16 control_id,uint32 value); /* 声明接口，设置仪表值。 */

/*! 
 *  \brief      设置滑动条
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  value 数值
 */
void SetSliderValue(uint16 screen_id,uint16 control_id,uint32 value); /* 声明接口，设置滑动条。 */

/*! 
 *  \brief      设置选择控件
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  item 当前选项
 */
void SetSelectorValue(uint16 screen_id,uint16 control_id,uint8 item); /* 声明接口，设置选择控件。 */

/*! 
 *  \brief      开始播放动画
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 */
void AnimationStart(uint16 screen_id,uint16 control_id); /* 声明接口，开始播放动画。 */

/*! 
 *  \brief      停止播放动画
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 */
void AnimationStop(uint16 screen_id,uint16 control_id); /* 声明接口，停止播放动画。 */

/*! 
 *  \brief      暂停播放动画
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 */
void AnimationPause(uint16 screen_id,uint16 control_id); /* 声明接口，暂停播放动画。 */

/*! 
 *  \brief     播放制定帧
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  frame_id 帧ID
 */
void AnimationPlayFrame(uint16 screen_id,uint16 control_id,uint8 frame_id); /* 声明接口，播放制定帧。 */

/*! 
 *  \brief     播放上一帧
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 */
void AnimationPlayPrev(uint16 screen_id,uint16 control_id); /* 声明接口，播放上一帧。 */

/*! 
 *  \brief     播放下一帧
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 */
void AnimationPlayNext(uint16 screen_id,uint16 control_id); /* 声明接口，播放下一帧。 */

/*! 
 *  \brief     曲线控件-添加通道
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  channel 通道号
 *  \param  color 颜色
 */
void GraphChannelAdd(uint16 screen_id,uint16 control_id,uint8 channel,uint16 color); /* 声明接口，曲线控件-添加通道。 */

/*! 
 *  \brief     曲线控件-删除通道
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  channel 通道号
 */
void GraphChannelDel(uint16 screen_id,uint16 control_id,uint8 channel); /* 声明接口，曲线控件-删除通道。 */

/*! 
 *  \brief     曲线控件-添加数据
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  channel 通道号
 *  \param  pData 曲线数据
 *  \param  nDataLen 数据个数
 */
void GraphChannelDataAdd(uint16 screen_id,uint16 control_id,uint8 channel,uint8 *pData,uint16 nDataLen); /* 声明接口，曲线控件-添加数据。 */

/*! 
 *  \brief     曲线控件-清除数据
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  channel 通道号
 */
void GraphChannelDataClear(uint16 screen_id,uint16 control_id,uint8 channel); /* 声明接口，曲线控件-清除数据。 */

/*! 
 *  \brief     曲线控件-设置视图窗口
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  x_offset 水平偏移
 *  \param  x_mul 水平缩放系数
 *  \param  y_offset 垂直偏移
 *  \param  y_mul 垂直缩放系数
 */
void GraphSetViewport(uint16 screen_id,uint16 control_id,int16 x_offset,uint16 x_mul,int16 y_offset,uint16 y_mul); /* 声明接口，曲线控件-设置视图窗口。 */

/*! 
 *  \brief     打开一个批量更新帧，之后只能追加对应记录，最后调用 BatchEnd。
 *  \param  screen_id 画面ID
 */
void BatchBegin(uint16 screen_id); /* 声明接口，打开一个批量更新帧，之后只能追加对应记录，最后调用 BatchEnd。 */

/*! 
 *  \brief     批量更新按钮控件
 *  \param  control_id 控件ID
 *  \param  state 按钮状态，写入当前批量更新帧
 */
void BatchSetButtonValue(uint16 control_id,uint8 state); /* 声明接口，批量更新按钮控件。 */

/*! 
 *  \brief     批量更新进度条控件
 *  \param  control_id 控件ID
 *  \param  value 数值
 */
void BatchSetProgressValue(uint16 control_id,uint32 value); /* 声明接口，批量更新进度条控件。 */

/*! 
 *  \brief     批量更新滑动条控件
 *  \param  control_id 控件ID
 *  \param  value 数值
 */
void BatchSetSliderValue(uint16 control_id,uint32 value); /* 声明接口，批量更新滑动条控件。 */

/*! 
 *  \brief     批量更新仪表控件
 *  \param  control_id 控件ID
 *  \param  value 数值
 */
void BatchSetMeterValue(uint16 control_id,uint32 value); /* 声明接口，批量更新仪表控件。 */

/*! 
 *  \brief     批量更新文本控件
 *  \param  control_id 控件ID
 *  \param  strings 字符串
 */
void BatchSetText(uint16 control_id,uchar *strings); /* 声明接口，批量更新文本控件。 */

/*! 
 *  \brief     批量更新动画\图标控件
 *  \param  control_id 控件ID
 *  \param  frame_id 帧ID
 */
void BatchSetFrame(uint16 control_id,uint16 frame_id); /* 声明接口，批量更新动画\图标控件。 */

#if FIRMWARE_VER>=908 /* 仅在「FIRMWARE_VER>=908」 成立时编译下方代码。 */

/*! 
 *  \brief     批量设置控件可见
 *  \param  control_id 控件ID
 *  \param  visible 帧ID
 */
void BatchSetVisible(uint16 control_id,uint8 visible); /* 声明接口，批量设置控件可见。 */

/*! 
 *  \brief     批量设置控件使能
 *  \param  control_id 控件ID
 *  \param  enable 帧ID
 */
void BatchSetEnable(uint16 control_id,uint8 enable); /* 声明接口，批量设置控件使能。 */

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

/*! 
 *  \brief    结束批量更新
 */
void BatchEnd(void); /* 声明接口，结束批量更新。 */

/*! 
 *  \brief     设置倒计时控件
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  timeout 倒计时(秒)
 */
void SeTimer(uint16 screen_id,uint16 control_id,uint32 timeout); /* 声明接口，设置倒计时控件。 */

/*! 
 *  \brief     开启倒计时控件
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 */
void StartTimer(uint16 screen_id,uint16 control_id); /* 声明接口，开启倒计时控件。 */

/*! 
 *  \brief     停止倒计时控件
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 */
void StopTimer(uint16 screen_id,uint16 control_id); /* 声明接口，停止倒计时控件。 */

/*! 
 *  \brief     暂停倒计时控件
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 */
void PauseTimer(uint16 screen_id,uint16 control_id); /* 声明接口，暂停倒计时控件。 */

/*! 
 *  \brief     设置控件背景色
 *  \details  支持控件：进度条、文本
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  color 背景色
 */
void SetControlBackColor(uint16 screen_id,uint16 control_id,uint16 color); /* 声明接口，设置控件背景色。 */

/*! 
 *  \brief     设置控件前景色
  * \details  支持控件：进度条
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  color 前景色
 */
void SetControlForeColor(uint16 screen_id,uint16 control_id,uint16 color); /* 声明接口，设置控件前景色。 */

/*! 
 *  \brief     显示\隐藏弹出菜单控件
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  show 是否显示，为0时focus_control_id无效
 *  \param  focus_control_id 关联的文本控件(菜单控件的内容输出到文本控件)
 */
void ShowPopupMenu(uint16 screen_id,uint16 control_id,uint8 show,uint16 focus_control_id); /* 声明接口，显示\隐藏弹出菜单控件。 */

/*! 
 *  \brief     显示\隐藏系统键盘
 *  \param  show 0隐藏，1显示
 *  \param  x 键盘显示位置X坐标
 *  \param  y 键盘显示位置Y坐标
 *  \param  type 0小键盘，1全键盘
 *  \param  option 0正常字符，1密码，2时间设置
 *  \param  max_len 键盘录入字符长度限制
 */
void ShowKeyboard(uint8 show,uint16 x,uint16 y,uint8 type,uint8 option,uint8 max_len); /* 声明接口，显示\隐藏系统键盘。 */

#if FIRMWARE_VER>=914 /* 仅在「FIRMWARE_VER>=914」 成立时编译下方代码。 */
/*! 
 *  \brief     多语言设置
 *  \param  ui_lang 用户界面语言0~9
 *  \param  sys_lang 系统键盘语言-0中文，1英文
 */
void SetLanguage(uint8 ui_lang,uint8 sys_lang); /* 声明接口，多语言设置。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

#if FIRMWARE_VER>=917 /* 仅在「FIRMWARE_VER>=917」 成立时编译下方代码。 */
/*! 
 *  \brief     开始保存控件数值到FLASH
 *  \param  version 数据版本号，可任意指定，高16位为主版本号，低16位为次版本号
 *  \param  address 数据在用户存储区的存放地址，注意防止地址重叠、冲突
 */
void FlashBeginSaveControl(uint32 version,uint32 address); /* 声明接口，开始保存控件数值到FLASH。 */

/*! 
 *  \brief     保存某个控件的数值到FLASH
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 */
void FlashSaveControl(uint16 screen_id,uint16 control_id); /* 声明接口，保存某个控件的数值到FLASH。 */

/*! 
 *  \brief     保存结束
 */
void FlashEndSaveControl(void); /* 声明接口，保存结束。 */

/*! 
 *  \brief     从FLASH中恢复控件数据
 *  \param  version 数据版本号，主版本号必须与存储时一致，否则会加载失败
 *  \param  address 数据在用户存储区的存放地址
 */
void FlashRestoreControl(uint32 version,uint32 address); /* 声明接口，从FLASH中恢复控件数据。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

#if FIRMWARE_VER>=921 /* 仅在「FIRMWARE_VER>=921」 成立时编译下方代码。 */
/*! 
 *  \brief     设置历史曲线采样数据值(单字节，uint8或int8)
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  value 采样点数据
 *  \param  channel 通道数
 */
void HistoryGraph_SetValueInt8(uint16 screen_id,uint16 control_id,uint8 *value,uint8 channel); /* 声明接口，设置历史曲线采样数据值(单字节，uint8或int8)。 */

/*! 
 *  \brief     设置历史曲线采样数据值(双字节，uint16或int16)
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  value 采样点数据
 *  \param  channel 通道数
 */
void HistoryGraph_SetValueInt16(uint16 screen_id,uint16 control_id,uint16 *value,uint8 channel); /* 声明接口，设置历史曲线采样数据值(双字节，uint16或int16)。 */

/*! 
 *  \brief     设置历史曲线采样数据值(四字节，uint32或int32)
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  value 采样点数据
 *  \param  channel 通道数
 */
void HistoryGraph_SetValueInt32(uint16 screen_id,uint16 control_id,uint32 *value,uint8 channel); /* 声明接口，设置历史曲线采样数据值(四字节，uint32或int32)。 */

/*! 
 *  \brief     设置历史曲线采样数据值(单精度浮点数)
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  value 采样点数据
 *  \param  channel 通道数
 */
void HistoryGraph_SetValueFloat(uint16 screen_id,uint16 control_id,float *value,uint8 channel); /* 声明接口，设置历史曲线采样数据值(单精度浮点数)。 */

/*! 
 *  \brief     允许或禁止历史曲线采样
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  enable 0-禁止，1-允许
 */
void HistoryGraph_EnableSampling(uint16 screen_id,uint16 control_id,uint8 enable); /* 声明接口，允许或禁止历史曲线采样。 */

/*! 
 *  \brief     显示或隐藏历史曲线通道
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  channel 通道编号
 *  \param  show 0-隐藏，1-显示
 */
void HistoryGraph_ShowChannel(uint16 screen_id,uint16 control_id,uint8 channel,uint8 show); /* 声明接口，显示或隐藏历史曲线通道。 */


/*! 
 *  \brief     设置历史曲线时间长度(即采样点数)
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  sample_count 一屏显示的采样点数
 */
void HistoryGraph_SetTimeLength(uint16 screen_id,uint16 control_id,uint16 sample_count); /* 声明接口，设置历史曲线时间长度(即采样点数)。 */

/*! 
 *  \brief     历史曲线缩放到全屏
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 */
void HistoryGraph_SetTimeFullScreen(uint16 screen_id,uint16 control_id); /* 声明接口，历史曲线缩放到全屏。 */

/*! 
 *  \brief     设置历史曲线缩放比例系数
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  zoom 缩放百分比(zoom>100%时水平方向缩小，反正放大)
 *  \param  max_zoom 缩放限制，一屏最多显示采样点数
  *  \param  min_zoom 缩放限制，一屏最少显示采样点数
 */
void HistoryGraph_SetTimeZoom(uint16 screen_id,uint16 control_id,uint16 zoom,uint16 max_zoom,uint16 min_zoom); /* 声明接口，设置历史曲线缩放比例系数。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

#if SD_FILE_EN /* 仅在「SD_FILE_EN」 成立时编译下方代码。 */
/*! 
 *  \brief     检测SD卡是否插入
 */
void SD_IsInsert(void); /* 声明接口，检测SD卡是否插入。 */

#define	FA_READ				        0x01  /*!< 可读取*/
#define	FA_WRITE			            0x02  /*!< 可写入*/
#define	FA_CREATE_NEW		    0x04  /*!< 创建新文件，如果文件已经存在，则返回失败*/
#define	FA_CREATE_ALWAYS	0x08  /*!< 创建新文件，如果文件已经存在，则覆盖*/
#define	FA_OPEN_EXISTING	    0x00  /*!< 打开文件，如果文件不存在，则返回失败*/
#define	FA_OPEN_ALWAYS		0x10  /*!< 打开文件，如果文件不存在，则创建新文件*/

/*! 
 *  \brief     打开或创建文件
 *  \param  filename 文件名称(仅ASCII编码)
 *  \param  mode 模式，可选组合模式如上FA_XXXX
 */
void SD_CreateFile(uint8 *filename,uint8 mode); /* 声明接口，打开或创建文件。 */

/*! 
 *  \brief     以当前时间创建文件，例如:20161015083000.txt
 *  \param  ext 文件后缀，例如 txt
 */
void SD_CreateFileByTime(uint8 *ext); /* 声明接口，以当前时间创建文件，例如:20161015083000.txt。 */

/*! 
 *  \brief     在当前文件末尾写入数据
 *  \param  buffer 数据
 *  \param  dlc 数据长度
 */
void SD_WriteFile(uint8 *buffer,uint16 dlc); /* 声明接口，在当前文件末尾写入数据。 */

/*! 
 *  \brief     读取当前文件
 *  \param  offset 文件位置偏移
 *  \param  dlc 数据长度
 */
void SD_ReadFile(uint32 offset,uint16 dlc); /* 声明接口，读取当前文件。 */

/*! 
 *  \brief     获取当前文件长度
 */
void SD_GetFileSize(); /* 声明接口，获取当前文件长度。 */

/*! 
 *  \brief     关闭当前文件
 */
void SD_CloseFile(); /* 声明接口，关闭当前文件。 */
#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */

/*! 
 *  \brief     记录控件-触发警告
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  value 告警值
 *  \param  time 告警产生的时间，为0时使用屏幕内部时间
 */
void Record_SetEvent(uint16 screen_id,uint16 control_id,uint16 value,uint8 *time); /* 声明接口，记录控件-触发警告。 */

/*! 
 *  \brief     记录控件-解除警告
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  value 告警值
 *  \param  time 告警解除的时间，为0时使用屏幕内部时间
 */
void Record_ResetEvent(uint16 screen_id,uint16 control_id,uint16 value,uint8 *time); /* 声明接口，记录控件-解除警告。 */

/*! 
 *  \brief    记录控件- 添加常规记录
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  record 一条记录(字符串)，子项通过分号隔开，例如：第一项;第二项;第三项;
 */
void Record_Add(uint16 screen_id,uint16 control_id,uint8 *record); /* 声明接口，记录控件- 添加常规记录。 */

/*! 
 *  \brief     记录控件-清除记录数据
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 */
void Record_Clear(uint16 screen_id,uint16 control_id); /* 声明接口，记录控件-清除记录数据。 */

/*! 
 *  \brief     记录控件-设置记录显示偏移
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  offset 显示偏移，滚动条位置
 */
void Record_SetOffset(uint16 screen_id,uint16 control_id,uint16 offset); /* 声明接口，记录控件-设置记录显示偏移。 */

/*! 
 *  \brief     记录控件-获取当前记录数目
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 */
void Record_GetCount(uint16 screen_id,uint16 control_id); /* 声明接口，记录控件-获取当前记录数目。 */

/*! 
 *  \brief     记录控件-读取指定位置的记录
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  index 索引
 */
void Record_Read(uint16 screen_id,uint16 control_id,uint16 index); /* 声明接口，记录控件-读取指定位置的记录。 */

/*! 
 *  \brief     记录控件-修改指定位置的记录
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  index 索引
 *  \param  record 一条记录(字符串)，子项通过分号隔开，例如：第一项;第二项;第三项;
 */
void Record_Modify(uint16 screen_id,uint16 control_id,uint16 index,uint8 *record); /* 声明接口，记录控件-修改指定位置的记录。 */

/*! 
 *  \brief     记录控件-删除指定位置的记录
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  index 索引
 */
void Record_Delete(uint16 screen_id,uint16 control_id,uint16 index); /* 声明接口，记录控件-删除指定位置的记录。 */

/*! 
 *  \brief     记录控件-在指定位置插入记录
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  index 索引
 *  \param  record 一条记录(字符串)，子项通过分号隔开，例如：第一项;第二项;第三项;
 */
void Record_Insert(uint16 screen_id,uint16 control_id,uint16 index,uint8 *record); /* 声明接口，记录控件-在指定位置插入记录。 */

/*! 
 *  \brief     记录控件-选中指定记录索引
 *  \param  screen_id 画面ID
 *  \param  control_id 控件ID
 *  \param  index 索引
 */
void Record_Select(uint16 screen_id,uint16 control_id,uint16 index); /* 声明接口，记录控件-选中指定记录索引。 */

/*! 
 *  \brief     读取屏幕RTC时间
 */
/* 返回 1 表示读屏幕 RTC 的指令已交给 DMA，返回 0 时由调用方稍后重试。 */
uint8 ReadRTC(void); /* 声明接口，提交一条屏幕读时命令，由主循环继续等待回包。 */

/*! 
 *  \brief     设置屏幕RTC时间
 *  \param    rtc_data 8字节二进制时间数组 (年16位大端,月,日,时,分,秒,星期)
 */
void WriteRTC(uint8 *rtc_data); /* 声明接口，设置屏幕RTC时间。 */

/*! 
 *  \brief     USART3 DMA 初始化后调用，清空接收缓冲和组帧状态并注册回调。
 */
void HMI_LinkInit(void); /* 声明接口，初始化 HMI 接收链路，清理历史接收状态并挂接回调。 */

/*! 
 *  \brief     主循环轮询 USART3 环形 DMA，组帧后调用 ProcessMessage。
 */
void HMI_LinkTask(void); /* 声明接口，轮询屏幕 DMA 并取出完整帧，在主循环上下文分发。 */

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */ //_HMI_DRIVER_
