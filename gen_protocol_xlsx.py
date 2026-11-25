# -*- coding: utf-8 -*-
"""生成《主板 上位机通信协议》详细表格文档 (xlsx)"""
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill, Alignment, Border, Side
from openpyxl.utils import get_column_letter

# 脚本离线输出硬编码的上位机协议方案。当前固件的 USART1 用于日志和调试输出，
# HMI 走 USART3 上的 EE…FF FC FF FF 帧。表中的 AA 55 协议没有接入当前固件。
# OUT 指定工作簿保存路径，运行时同路径的原文件会被覆盖。
OUT = r"C:\Users\汪剑\Desktop\主板上位机通信协议.xlsx"

# ---------- 样式 ----------
TITLE_FONT = Font(bold=True, size=15, color="FFFFFF")
TITLE_FILL = PatternFill("solid", fgColor="2F5496")
NOTE_FONT = Font(size=10, color="404040", italic=True)
HEADER_FONT = Font(bold=True, color="FFFFFF")
HEADER_FILL = PatternFill("solid", fgColor="4472C4")
SUB_FONT = Font(bold=True, color="1F3864")
SUB_FILL = PatternFill("solid", fgColor="D9E2F3")
thin = Side(style="thin", color="A6A6A6")
BORDER = Border(left=thin, right=thin, top=thin, bottom=thin)
WRAP = Alignment(wrap_text=True, vertical="top")
CENTER = Alignment(horizontal="center", vertical="center", wrap_text=True)


def set_widths(ws, widths):
    # openpyxl 的列编号从 1 开始，宽度按列顺序传入，再转换为 A、B、C 等列名。
    for i, w in enumerate(widths, 1):
        ws.column_dimensions[get_column_letter(i)].width = w


def sheet_title(ws, text, note=None):
    # 统一合并前六列作为标题，可选说明占第 2 行。返回下一个可放表格的行号。
    ws.merge_cells(start_row=1, start_column=1, end_row=1, end_column=6)
    c = ws.cell(row=1, column=1, value=text)
    c.font = TITLE_FONT
    c.fill = TITLE_FILL
    c.alignment = Alignment(horizontal="left", vertical="center")
    ws.row_dimensions[1].height = 26
    r = 2
    if note:
        ws.merge_cells(start_row=2, start_column=1, end_row=2, end_column=6)
        nc = ws.cell(row=2, column=1, value=note)
        nc.font = NOTE_FONT
        nc.alignment = Alignment(wrap_text=True, vertical="top")
        ws.row_dimensions[2].height = 28
        r = 3
    return r


def write_table(ws, start_row, headers, rows, widths=None, center_cols=None):
    """写表头+数据，返回下一行"""
    # center_cols 用从 1 开始的列号指定居中列，其余列采用可换行样式。
    # widths 提供时会设置整张工作表的列宽，方便同页的多块表格共用布局。
    center_cols = center_cols or set()
    for j, h in enumerate(headers, 1):
        c = ws.cell(row=start_row, column=j, value=h)
        c.font = HEADER_FONT
        c.fill = HEADER_FILL
        c.alignment = CENTER
        c.border = BORDER
    r = start_row + 1
    for row in rows:
        for j, v in enumerate(row, 1):
            c = ws.cell(row=r, column=j, value=v)
            c.border = BORDER
            c.alignment = CENTER if j in center_cols else WRAP
        r += 1
    if widths:
        set_widths(ws, widths)
    return r


# 新工作簿自带一个空表，协议总览直接复用它，后续主题逐张添加工作表。
wb = Workbook()

# ============ Sheet 1 协议总览 ============
ws = wb.active
ws.title = "1-协议总览"
r = sheet_title(ws, "STM32F103RC 主板 · 上位机通信协议（总览）",
                "串口参数：115200 波特率 · 8 数据位 · 1 停止位 · 无校验 (8N1)    |    "
                "通信口：USART1 (PA9=TX, PA10=RX)，经 USB-TTL 小板连电脑    |    "
                "所有多字节字段一律“低字节在前”(小端)")
r = write_table(ws, r,
    ["字段", "字节位置", "长度", "示例值", "作用说明"],
    [
        ["帧头", "0 ~ 1", "2 字节", "AA 55", "帧起始标志，接收端靠它对齐/同步，识别一帧的开始"],
        ["命令字 CMD", "2", "1 字节", "0x01 / 0x81", "表示这一帧要干什么，0x0x=上位机下发，0x8x=主板上报"],
        ["数据长度 LEN", "3", "1 字节", "0x00 ~ 0xFF", "数据区字节数，解帧的关键锚点：靠它数够数据长度，避免误判"],
        ["数据区 DATA", "4 ~ 3+LEN", "N 字节", "见各命令", "命令的具体参数，内容随命令字变化，长度由 LEN 决定"],
        ["CRC16", "4+LEN ~ 5+LEN", "2 字节", "低字节在前", "CRC16-Modbus 校验，检测传输错误；只算 CMD+LEN+DATA"],
        ["帧尾", "6+LEN ~ 7+LEN", "2 字节", "0D 0A", "帧结束辅助标记(回车换行)，最后一层完整性确认"],
    ],
    widths=[16, 14, 10, 16, 60], center_cols={2, 3})
r += 1
r = write_table(ws, r,
    ["项目", "约定", "说明"],
    [
        ["字节序", "低字节在前 (小端)", "多字节字段(压力值、CRC16)一律低字节在前，与 STM32 内存顺序一致"],
        ["CRC 算法", "CRC16-Modbus", "初始值 0xFFFF，多项式 0xA001 (低字节在前)；只对 命令字+长度+数据区 计算"],
        ["总帧长", "6 + LEN 字节", "帧头2 + 命令1 + 长度1 + 数据N + CRC2 + 帧尾2"],
        ["压力值", "ADC 原始值 0~4095", "由 Liquid_Level_Read() 读取，100ms 采样，上报为 2 字节无符号整数"],
        ["状态上报", "周期主动上报", "主板每 200~500ms 主动发一帧状态，上位机被动接收刷新"],
    ],
    widths=[16, 22, 70])
r += 1
c = ws.cell(row=r, column=1, value="CRC16-Modbus 伪代码：crc=0xFFFF; 对每个字节 b: crc^=b; 循环8次: crc = (crc&1) ? (crc>>1)^0xA001 : crc>>1")
c.font = Font(size=10, name="Consolas")
c.alignment = WRAP
ws.merge_cells(start_row=r, start_column=1, end_row=r, end_column=6)
ws.row_dimensions[r].height = 20

# ============ Sheet 2 命令总表 ============
ws = wb.create_sheet("2-命令总表")
r = sheet_title(ws, "命令总表", "0x0x = 上位机(电脑) → 主板；0x8x = 主板 → 上位机")
r = write_table(ws, r,
    ["命令字", "方向", "含义", "数据区结构", "LEN", "响应/说明"],
    [
        ["0x01", "上位机 → 主板", "控制单个器件", "器件编号(1B) + 开关(1B)", "2", "主板执行后回 0x82 应答"],
        ["0x02", "上位机 → 主板", "读全部状态", "无", "0", "主板回一帧 0x81 状态上报"],
        ["0x03", "上位机 → 主板", "一键全关(急停)", "无", "0", "调用 set_all_gpio_low()，回 0x82 应答"],
        ["0x81", "主板 → 上位机", "状态上报", "阀位图(1B)+压力低(1B)+压力高(1B)+流程(1B)", "4", "周期主动上报(200~500ms) 或应答 0x02"],
        ["0x82", "主板 → 上位机", "命令应答", "原命令字(1B) + 结果(1B)", "2", "结果 0x00=成功, 0xFF=失败"],
    ],
    widths=[12, 18, 18, 42, 8, 44], center_cols={1, 2, 5})
r += 1
r = write_table(ws, r,
    ["命令", "数据区字节分解(逐字节)"],
    [
        ["0x01 控制器件", "字节1=器件编号(0x01~0x05)；字节2=开关(0x01开 / 0x00关)"],
        ["0x02 读状态", "无数据区 (LEN=0)"],
        ["0x03 一键全关", "无数据区 (LEN=0)"],
        ["0x81 状态上报", "字节1=阀位图(bit0~bit4 对应5器件)；字节2=压力低字节；字节3=压力高字节；字节4=流程标志"],
        ["0x82 命令应答", "字节1=原命令字(回显)；字节2=结果(0x00成功 / 0xFF失败)"],
    ],
    widths=[18, 90])

# ============ Sheet 3 帧格式字段详解 ============
ws = wb.create_sheet("3-帧格式字段详解")
r = sheet_title(ws, "帧格式字段详解", "完整帧 = 帧头(2) + 命令字(1) + 长度(1) + 数据区(N) + CRC16(2) + 帧尾(2)")
r = write_table(ws, r,
    ["字段", "长度", "取值/范围", "作用", "详细说明与注意事项"],
    [
        ["帧头", "2B", "固定 AA 55", "帧同步/对齐",
         "二进制 10101010 / 01010101，逐位互补跳变，波形最规整易识别；连续出现概率低。必须是 AA 后跟 55，反了不算。"],
        ["命令字 CMD", "1B", "0x00~0xFF", "帧功能标识",
         "共 256 种可用。0x0x=上位机下发(01/02/03)，0x8x=主板上报(81/82)。新增命令时先分方向再编号。"],
        ["数据长度 LEN", "1B", "0x00~0xFF", "数据区字节数",
         "解帧关键锚点：数据区可能碰巧出现 AA 55 / 0D 0A，不能靠找帧头帧尾判断结束，必须按 LEN 数够 N 字节。"],
        ["数据区 DATA", "N 字节", "随命令变化", "命令参数",
         "内容因命令字而异，详见 Sheet 2 和 Sheet 7。长度必须与 LEN 一致。"],
        ["CRC16", "2B", "低字节在前", "传输校验",
         "CRC16-Modbus，只对 CMD+LEN+DATA 计算，不含帧头/CRC自己/帧尾。接收端重算比对，不一致丢弃。"],
        ["帧尾", "2B", "固定 0D 0A", "帧结束标记",
         "0D 0A=回车换行，串口助手查看时自动换行清晰。仅作兜底校验，真正切帧靠 LEN。"],
    ],
    widths=[14, 8, 16, 12, 70], center_cols={2})

# ============ Sheet 4 器件编号表 ============
ws = wb.create_sheet("4-器件编号表")
r = sheet_title(ws, "器件编号表（5 个执行器件）", "用于 0x01 控制命令的数据区字节1；开关字节：0x01=开(置高) / 0x00=关(置低)，其余值非法")
r = write_table(ws, r,
    ["编号", "器件名称", "引脚", "控制函数(已存在)", "状态标志(已存在)", "说明"],
    [
        ["0x01", "循环泵", "PA6", "circulation_pump_Set()", "FLAG_CIRCULATION_PUMP", "循环水路动力泵"],
        ["0x02", "进水电磁阀", "PA7", "water_inlet_solenoid_valve_Set()", "FLAG_WATER_INLET_VALVE", "进水控制"],
        ["0x03", "清洗电磁阀", "PA8", "cleaning_solenoid_valve_Set()", "FLAG_CLEANING_VALVE", "清洗水路控制"],
        ["0x04", "循环三通阀", "PC4", "Circulation_Three_Way_Valve_Set()", "FLAG_CIRCULATION_THREE_WAY", "循环回路切换"],
        ["0x05", "出液三通阀", "PC6", "Liquid_Outlet_Three_Way_Valve_Set()", "FLAG_LIQUID_OUTLET_THREE_WAY", "出液/排液切换"],
    ],
    widths=[10, 16, 10, 42, 34, 22], center_cols={1, 3})
r += 1
r = write_table(ws, r,
    ["开关字节取值", "含义", "备注"],
    [
        ["0x01", "打开（GPIO 置高）", "调用对应 Set(1)"],
        ["0x00", "关闭（GPIO 置低）", "调用对应 Set(0)"],
        ["0x02~0xFF", "非法", "主板回 0x82 应答(结果=0xFF 失败)"],
    ],
    widths=[16, 24, 40], center_cols={1})

# ============ Sheet 5 阀位图映射 ============
ws = wb.create_sheet("5-阀位图映射")
r = sheet_title(ws, "阀位图位映射（状态上报 0x81 数据区字节1）",
                "1 个字节同时表示 5 个器件的开关状态：bit=1 表示开，bit=0 表示关。多个器件同时开时，把对应权值相加。")
r = write_table(ws, r,
    ["位", "权值", "器件", "对应状态标志", "bit=1", "bit=0"],
    [
        ["bit0", 1, "循环泵", "FLAG_CIRCULATION_PUMP", "开", "关"],
        ["bit1", 2, "进水电磁阀", "FLAG_WATER_INLET_VALVE", "开", "关"],
        ["bit2", 4, "清洗电磁阀", "FLAG_CLEANING_VALVE", "开", "关"],
        ["bit3", 8, "循环三通阀", "FLAG_CIRCULATION_THREE_WAY", "开", "关"],
        ["bit4", 16, "出液三通阀", "FLAG_LIQUID_OUTLET_THREE_WAY", "开", "关"],
        ["bit5", 32, "保留", "—", "—", "—"],
        ["bit6", 64, "保留", "—", "—", "—"],
        ["bit7", 128, "保留", "—", "—", "—"],
    ],
    widths=[10, 10, 18, 36, 10, 10], center_cols={1, 2, 5, 6})
r += 1
r = write_table(ws, r,
    ["示例场景", "开的器件", "阀位图字节(十六进制)", "计算过程"],
    [
        ["全部关闭", "无", "0x00", "0"],
        ["只开循环泵", "循环泵", "0x01", "1"],
        ["三个开(泵+进水阀+清洗阀)", "循环泵,进水阀,清洗阀", "0x07", "1+2+4=7"],
        ["三个开(泵+循环三通+出液三通)", "循环泵,循环三通阀,出液三通阀", "0x19", "1+8+16=25"],
        ["五个全开", "全部", "0x1F", "1+2+4+8+16=31"],
    ],
    widths=[34, 32, 26, 30], center_cols={3})

# ============ Sheet 6 流程标志映射 ============
ws = wb.create_sheet("6-流程标志映射")
r = sheet_title(ws, "流程标志位映射（状态上报 0x81 数据区字节4）",
                "表示主板当前正在运行的流程，bit=1 表示该流程进行中；可多位同时置 1。")
r = write_table(ws, r,
    ["位", "权值", "流程名称", "对应状态标志"],
    [
        ["bit0", 1, "清洗流程", "FLAG_WASH_START"],
        ["bit1", 2, "萃取流程(咖啡)", "FLAG_COFFEE_START"],
        ["bit2", 4, "排废液流程", "FLAG_DRAIN_WASTE"],
        ["bit3", 8, "排萃取液流程", "FLAG_DRAIN_BREW"],
        ["bit4", 16, "保留", "—"],
        ["bit5", 32, "保留", "—"],
        ["bit6", 64, "保留", "—"],
        ["bit7", 128, "保留", "—"],
    ],
    widths=[10, 10, 22, 36], center_cols={1, 2})

# ============ Sheet 7 数据区详解 ============
ws = wb.create_sheet("7-数据区详解")
r = sheet_title(ws, "数据区详解（逐字节）", "数据区长度由 LEN 字段指定；不同命令字的数据区结构完全不同。")
r = write_table(ws, r,
    ["命令字", "LEN", "字节序号", "字段名", "取值", "含义"],
    [
        ["0x01 控制器件", "2", "数据区字节1", "器件编号", "0x01~0x05", "目标器件，见 Sheet 4 器件编号表"],
        ["0x01 控制器件", "2", "数据区字节2", "开关", "0x01 / 0x00", "0x01=开, 0x00=关"],
        ["0x02 读状态", "0", "—", "—", "—", "无数据区"],
        ["0x03 一键全关", "0", "—", "—", "—", "无数据区"],
        ["0x81 状态上报", "4", "数据区字节1", "阀位图", "0x00~0x1F", "5 器件开关状态，见 Sheet 5"],
        ["0x81 状态上报", "4", "数据区字节2", "压力低字节", "0x00~0xFF", "压力 ADC 值低 8 位"],
        ["0x81 状态上报", "4", "数据区字节3", "压力高字节", "0x00~0x0F", "压力 ADC 值高 8 位；压力=低字节+高字节×256"],
        ["0x81 状态上报", "4", "数据区字节4", "流程标志", "0x00~0x0F", "当前运行流程，见 Sheet 6"],
        ["0x82 命令应答", "2", "数据区字节1", "原命令字", "0x01~0x03", "回显应答的是哪条命令"],
        ["0x82 命令应答", "2", "数据区字节2", "结果", "0x00 / 0xFF", "0x00=成功, 0xFF=失败"],
    ],
    widths=[16, 8, 16, 16, 16, 44], center_cols={2, 3, 5})

# ============ Sheet 8 完整帧实例 ============
ws = wb.create_sheet("8-完整帧实例")
r = sheet_title(ws, "完整帧实例（逐字节拆解，含校验值）",
                "所有帧十六进制字符串可直接用串口助手发送/核对。CRC 为低字节在前。")
# 示例为预先填好的文本。本脚本写表时不会据字段重新计算帧长度或 CRC，
# 最后两条示例用于演示扩展位图。计算总帧长时，固定字段合计 8 字节。
examples = [
    ["1", "循环泵 开", "AA 55", "01", "02", "01 01", "60 48", "0D 0A", "AA 55 01 02 01 01 60 48 0D 0A"],
    ["2", "进水阀 关", "AA 55", "01", "02", "02 00", "A1 78", "0D 0A", "AA 55 01 02 02 00 A1 78 0D 0A"],
    ["3", "出液三通阀 开", "AA 55", "01", "02", "05 01", "62 88", "0D 0A", "AA 55 01 02 05 01 62 88 0D 0A"],
    ["4", "读全部状态", "AA 55", "02", "00", "(空)", "00 D0", "0D 0A", "AA 55 02 00 00 D0 0D 0A"],
    ["5", "一键全关", "AA 55", "03", "00", "(空)", "01 40", "0D 0A", "AA 55 03 00 01 40 0D 0A"],
    ["6", "状态上报(泵+三通开,压力2345)", "AA 55", "81", "04", "09 29 09 00", "3B CE", "0D 0A", "AA 55 81 04 09 29 09 00 3B CE 0D 0A"],
    ["7", "状态上报(泵+清洗开,压力512)", "AA 55", "81", "04", "05 00 02 00", "EE 66", "0D 0A", "AA 55 81 04 05 00 02 00 EE 66 0D 0A"],
    ["8", "状态上报(全关,压力4095,排废液)", "AA 55", "81", "04", "00 FF 0F 04", "DB C9", "0D 0A", "AA 55 81 04 00 FF 0F 04 DB C9 0D 0A"],
    ["9", "状态上报(泵+进水+清洗开,压力800)", "AA 55", "81", "04", "07 20 03 00", "EF 84", "0D 0A", "AA 55 81 04 07 20 03 00 EF 84 0D 0A"],
    ["10", "状态上报(泵+循环三通+出液三通开,800)", "AA 55", "81", "04", "19 20 03 00", "E9 AC", "0D 0A", "AA 55 81 04 19 20 03 00 E9 AC 0D 0A"],
    ["11", "应答-控制命令成功", "AA 55", "82", "02", "01 00", "88 0C", "0D 0A", "AA 55 82 02 01 00 88 0C 0D 0A"],
    ["12", "应答-控制命令失败", "AA 55", "82", "02", "01 FF", "C8 4C", "0D 0A", "AA 55 82 02 01 FF C8 4C 0D 0A"],
    ["13", "状态上报(第6个阀开,压力800)", "AA 55", "81", "04", "20 20 03 00", "E5 30", "0D 0A", "AA 55 81 04 20 20 03 00 E5 30 0D 0A"],
    ["14", "状态上报(2字节位图:器件1,9,10开)", "AA 55", "81", "05", "01 03 20 03 00", "B6 E3", "0D 0A", "AA 55 81 05 01 03 20 03 00 B6 E3 0D 0A"],
]
r = write_table(ws, r,
    ["序号", "说明", "帧头", "命令字", "LEN", "数据区", "CRC", "帧尾", "完整帧(十六进制)"],
    examples,
    widths=[6, 38, 10, 10, 8, 26, 12, 10, 60], center_cols={1, 3, 4, 5, 7, 8})
r += 1
r = write_table(ws, r,
    ["实例编号", "数据区含义拆解"],
    [
        ["6", "阀位图 09=泵+循环三通阀开；压力 29 09=2345；流程 00=无"],
        ["7", "阀位图 05=泵+清洗阀开；压力 00 02=512；流程 00=无"],
        ["8", "阀位图 00=全关；压力 FF 0F=4095(满量程)；流程 04=排废液中"],
        ["9", "阀位图 07=泵+进水阀+清洗阀开；压力 20 03=800"],
        ["10", "阀位图 19=泵+循环三通+出液三通开；压力 20 03=800"],
        ["13", "阀位图 20=bit5(第6个器件)开，其余关"],
        ["14", "2字节位图 01 03：低字节=器件1开；高字节=器件9,10开(扩展方案示例)"],
    ],
    widths=[14, 90])

# ============ Sheet 9 扩展方案 ============
ws = wb.create_sheet("9-扩展方案")
r = sheet_title(ws, "扩展方案（未来增加器件时）", "当前协议用 1 字节位图，最多表示 8 个器件；现用 5 个，bit5~bit7 空闲。")
r = write_table(ws, r,
    ["场景", "做法", "说明"],
    [
        ["加到 6~8 个器件", "直接用空闲位 bit5/bit6/bit7", "协议完全不变，只需新增位映射定义"],
        ["加到 9~16 个器件", "阀位图扩成 2 字节", "低字节=器件1~8，高字节=器件9~16；LEN 由 4 变 5"],
        ["加到 17 个以上", "位图继续扩 3/4 字节", "规则相同，始终低字节在前"],
        ["控制命令不受影响", "器件编号 0x06、0x07…", "编号本就是 1 字节(0x01~0xFF)，最多 255 个器件，无需改协议"],
    ],
    widths=[22, 34, 60])
r += 1
r = write_table(ws, r,
    ["扩展点", "当前值", "扩展后", "影响范围"],
    [
        ["阀位图长度", "1 字节(bit0~bit7)", "2 字节(bit0~bit15)", "0x81 数据区 +1 字节，LEN 4→5"],
        ["状态帧 LEN", "0x04", "0x05", "帧总长 11→12 字节"],
        ["器件编号上限", "0x05(已用5个)", "0xFF(255个)", "无需改动"],
    ],
    widths=[18, 26, 26, 40], center_cols={1})

# 冻结每张表的前两行。滚动查看协议条目时，顶部标题和说明仍能看到。
for ws in wb.worksheets:
    ws.freeze_panes = "A3"

wb.save(OUT)
print("SAVED:", OUT)
print("sheets:", wb.sheetnames)
