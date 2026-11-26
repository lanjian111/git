#include "stm32f10x.h" /* 使用 STM32F1 的寄存器、外设类型和基于工程配置启用的驱动接口。 */
#include "delay.h" /* 使用 SysTick 毫秒计时和短时延时接口。 */
#include "GPIO.h" /* 取得板级泵阀控制和液位采样接口。 */
#include "flag.h"                                                   // 引入流程标志位定义
#include "coffemake.h"                                              // 引入萃取流程接口声明
#include "elog.h"                                                   // EasyLogger 统一日志接口
#include <stdlib.h>                                                 // atof 字符串转浮点

/* 萃取与两种排液各有状态机，共用 GPIO 和缓存的 ADC 液位。
 * 100 ms 调度按清洗、萃取、排废液、排萃取液顺序调用。各流程依据自己的启动标志推进，
 * 共享阀泵和采样开关，当前调度没有统一的流程互锁。 */
#define LOG_TAG                         "coffee" /* 指定日志标签 "coffee"，便于在串口输出中定位本模块。 */

/* ========================== ADC 液位映射 ========================== */
#define ADC_0L                             100U                      // ADC值对应0升
#define ADC_5L                             3000U                     // ADC值对应5升
#define ADC_PER_LITER                      ((ADC_5L - ADC_0L) / 5)   // 每升对应的ADC单位 (580)
#define ADC_1L                             (ADC_0L + ADC_PER_LITER)  // ADC值对应1升 (680)

/* ========================== 时序参数 ========================== */
#define COFFEE_STAGE1_TIMEOUT_MS           30000UL                   // 第1段进水超时：30秒
#define COFFEE_STAGE2_PER_LITER_MS         30000UL                   // 第2段每升用时30秒

/* ========================== 液位抖动过滤 ========================== */
#define COFFEE_LEVEL_DEBOUNCE_COUNT        3U                        // 液位阈值连续确认次数（每次100ms）

/* ========================== 屏幕常量 ========================== */
#define SCREEN_ID_COFFEE                   0x0003U                   // 历史常量，当前未引用。实际萃取页由 cmd_process.c 按 0x0004 分发

/* ========================== 状态机 ========================== */
typedef enum /* 定义枚举的取值集合，供相关接口复用。 */
{
	COFFEE_STATE_IDLE = 0,                                            // 空闲状态，等待启动
	COFFEE_STATE_INIT,                                                // 初始化状态，关闭阀泵并开启液位采样
	COFFEE_STATE_STAGE1_FILL,                                         // 第1段进水：30秒内必须达到1L
	COFFEE_STATE_STAGE2_FILL,                                         // 第2段进水：继续进水到目标液位
	COFFEE_STATE_BREW,                                                // 萃取阶段：开循环泵，按设定时间计时
	COFFEE_STATE_DRAIN_DECISION,                                      // 排水决策：判断自动/手动
	COFFEE_STATE_DRAIN,                                               // 历史状态，无正常入口。自动排液由独立任务推进
	COFFEE_STATE_DONE,                                                // 萃取完成
	COFFEE_STATE_ERROR                                                // 故障状态
} CoffeeState; /* 为上述定义命名为 CoffeeState，便于在声明中引用。 */

/* ========================== 静态变量 ========================== */
static CoffeeState g_coffee_state = COFFEE_STATE_IDLE;               // 当前萃取状态
static uint32_t g_coffee_state_start_ms = 0;                         // 当前状态开始时间戳（用于阶段超时）
static uint32_t g_brew_start_ms = 0;                                  // 萃取阶段开始时间戳
static float    g_target_liters = 0.0f;                               // 屏幕设定的目标液量（升）
static uint16_t g_target_adc = 0;                                     // 目标液量对应的ADC值
static uint16_t g_brew_minutes = 0;                                   // 屏幕设定的萃取时间（分钟）
static uint8_t  g_drain_mode = 0;                                     // 排水模式：0=自动, 1=手动
static uint16_t g_protect_adc = 0;                                    // 萃取阶段液位保护阈值 = (N-1)L 对应ADC
static uint8_t  g_level_debounce_cnt = 0;                             // 液位达标防抖计数器

/* ========================== 辅助函数 ========================== */

static void coffee_set_inlet(uint8_t state)                          // 设置进水电磁阀且避免重复下发
{
	if (FLAG_WATER_INLET_VALVE != state) /* 比较请求与进水电磁阀状态镜像，只有状态改变时才再次下发硬件输出。 */
	{
		water_inlet_solenoid_valve_Set(state); /* 将请求状态传给进水电磁阀的控制接口，以更新相应输出。 */
	}
}

static void coffee_set_pump(uint8_t state)                           // 设置循环泵且避免重复下发
{
	if (FLAG_CIRCULATION_PUMP != state) /* 比较请求与循环泵输出状态镜像，只有状态改变时才再次下发硬件输出。 */
	{
		circulation_pump_Set(state); /* 将请求状态传给循环泵的控制接口，以更新相应输出。 */
	}
}

static void coffee_set_all_closed(void)                              // 关闭萃取直接控制的进水阀和循环泵
{
	coffee_set_pump(0); /* 关闭循环泵，使泵阀按当前流程阶段工作。 */
	coffee_set_inlet(0); /* 关闭进水电磁阀，使泵阀按当前流程阶段工作。 */
}

static void coffee_change_state(CoffeeState next_state, const char *message) /* 萃取 change 状态。 */
{
	g_coffee_state = next_state; /* 将当前萃取状态 更新为 「next_state」。 */
	log_i("%s", message); /* 以信息级别记录 「"%s", message」，便于从日志跟踪当前分支。 */
}

/* ========================== 屏幕参数接收接口 ========================== */

/*
 * 由 cmd_process.c 在收到萃取页面文本控件通知时调用
 * control_id:
 *   0x0001 — 液量设定（字符串，如 "3.5"）
 *   0x0002 — 时间设定（字符串，如 "10"）
 * 输入须以 NUL 结尾。参数写入立即生效，状态机读取这些共享配置。
 * 液量按 1~5 L 限幅后线性换算。分钟整数先转为无符号，再限幅到 1~60。
 */
void Coffee_SetParam(uint16_t control_id, uint8_t *str) /* 解析触摸屏文本参数，更新目标液量或萃取分钟数。 */
{
	if (str == 0) return; /* 参数为空或长度为零时提前返回，避免进行无效读写。 */

	switch (control_id) /* 按控件编号的取值选择对应的处理分支。 */
	{
	case 0x0001:                                                      // 液量设定
	{
		float val = (float)atof((const char *)str); /* 定义val，将val 更新为 「(float)atof((const char *)str)」。 */
		if (val < 1.0f) val = 1.0f; /* 把小于 1 L 的液量调到最低设定值，统一后续阈值计算的范围。 */
		if (val > 5.0f) val = 5.0f; /* 把超过 5 L 的液量限制为 5 L，与液位标定的上限一致。 */
		g_target_liters = val; /* 保存限幅后的目标液量，供进水时间和采样阈值换算。 */
		g_target_adc = (uint16_t)(ADC_0L + (uint32_t)(val * (float)ADC_PER_LITER)); /* 按零升偏移和每升 ADC 增量换算目标值，状态机直接用原始采样值比较。 */
		// 保护阈值 = (设定升数 - 1) 对应的ADC
		if (val > 1.0f) /* 保护阈值 = (设定升数 - 1) 对应的ADC。 */
		{
			g_protect_adc = (uint16_t)(ADC_0L + (uint32_t)((val - 1.0f) * (float)ADC_PER_LITER)); /* 将目标液量减去 1 L 后换算 ADC，作为萃取期间监测液位下降的阈值。 */
		}
		else /* 前面的条件不满足时执行此分支。 */
		{
			g_protect_adc = ADC_0L;                                   // 设定1L时，保护阈值即为0L
		}
		log_i("Target set: %.1f L -> ADC=%u, protect ADC=%u", (double)val, g_target_adc, g_protect_adc); /* 以信息级别记录 「"Target set: %.1f L -> ADC=%u, protect ADC=%u", (double)val, g_target_adc, g_protect_adc」，便于从日志跟踪当前分支。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
	}
	case 0x0002:                                                      // 萃取时间设定
	{
		uint32_t val = (uint32_t)atoi((const char *)str); /* 定义val，将val 更新为 「(uint32_t)atoi((const char *)str)」。 */
		if (val < 1)  val = 1; /* 把转换后小于 1 的分钟数调到最短萃取时长。 */
		if (val > 60) val = 60; /* 把转换后大于 60 的值限制为 60 分钟，避免设置过长。 */
		g_brew_minutes = (uint16_t)val; /* 保存限幅后的分钟数，萃取阶段据此计算结束时间。 */
		log_i("Brew time set: %u min", g_brew_minutes); /* 以信息级别记录 「"Brew time set: %u min", g_brew_minutes」，便于从日志跟踪当前分支。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
	}
	default: /* 处理没有被列出的其余取值。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
	}
}

/*
 * 由 cmd_process.c 在收到萃取页面选择器通知时调用
 * 选择器 ID 为 0x0003。item 原样保存，流程决策仅将 1 当作手动，其他值走自动。
 */
void Coffee_SetDrainMode(uint8_t item) /* 保存屏幕下发的排液模式，在萃取完成后决定是否启动自动排液。 */
{
	g_drain_mode = item; /* 将自动或手动排液选择 更新为 「item」。 */
	log_i("Drain mode set: %s", (item == 0) ? "AUTO" : "MANUAL"); /* 以信息级别记录 「"Drain mode set: %s", (item == 0) ? "AUTO" : "MANUAL"」，便于从日志跟踪当前分支。 */
}

/* ========================== 外部接口 ========================== */

void Coffee_ResetState(void)                                          // 关进水阀和泵并清计时，保留设定参数与排液标志
{
	FLAG_LIQUID_LEVEL_SAMPLE = 0; /* 清零液位 ADC 采样使能标志，供后续任务判断当前状态。 */
	coffee_set_all_closed(); /* 执行 coffee_set_all_closed 的萃取 设置 all closed操作。 */
	g_level_debounce_cnt = 0; /* 将液位连续达标次数清零，使本轮从初始计数开始。 */
	coffee_change_state(COFFEE_STATE_IDLE, "COFFEE_STATE_IDLE"); /* 更新到 COFFEE_STATE_IDLE 并输出状态日志，后续周期从该状态继续推进。 */
	g_coffee_state_start_ms = 0; /* 将萃取当前阶段的计时起点 更新为 「0」。 */
	g_brew_start_ms = 0; /* 将正式萃取的计时起点 更新为 「0」。 */
}

/* ========================================================================
 * 排水流程 — 通用宏定义
 * ======================================================================== */

#define DRAIN_LEVEL_LOW_THRESHOLD           100U                      // 排水结束液位阈值（ADC）
#define DRAIN_LEVEL_DEBOUNCE_COUNT          3U                        // 泵开启后连续读到 ADC ≤ 阈值的确认次数
#define DRAIN_DELAY_12S_MS                  12000UL                   // 开阀后等待12秒
#define DRAIN_PUMP_TIMEOUT_MS               60000UL                   // 循环泵排水60秒超时
#define DRAIN_LOW_HOLD_MS                   2000UL                    // 液位低于阈值后继续保持2秒

/* ========================================================================
 * 排废液状态机
 * ======================================================================== */

typedef enum /* 定义枚举的取值集合，供相关接口复用。 */
{
	DRAIN_WASTE_STATE_IDLE = 0,                                       // 空闲
	DRAIN_WASTE_STATE_INIT,                                           // 初始化：关所有阀，读液位
	DRAIN_WASTE_STATE_OPEN_CIRC,                                      // 开循环三通阀，等12s（跳过出液三通）
	DRAIN_WASTE_STATE_PUMP_ON,                                        // 开循环泵排水，60s超时
	DRAIN_WASTE_STATE_LOW_HOLD,                                       // 液位≤100后继续保持2s
	DRAIN_WASTE_STATE_DONE,                                           // 完成
	DRAIN_WASTE_STATE_ERROR                                           // 故障
} DrainWasteState; /* 为上述定义命名为 DrainWasteState，便于在声明中引用。 */

static DrainWasteState g_drain_waste_state = DRAIN_WASTE_STATE_IDLE; /* 定义当前排废液状态，切换到 DRAIN_WASTE_STATE_IDLE，下一次状态分发按该状态继续。 */
static uint32_t g_drain_waste_start_ms = 0; /* 将g 排液 废液 启动 毫秒初始化为零，给模块一个确定的起始值。 */
static uint8_t  g_drain_waste_low_cnt = 0; /* 将g 排液 废液 low cnt初始化为零，给模块一个确定的起始值。 */

/* ========================================================================
 * 排萃取液状态机
 * ======================================================================== */

typedef enum /* 定义枚举的取值集合，供相关接口复用。 */
{
	DRAIN_BREW_STATE_IDLE = 0,                                        // 空闲
	DRAIN_BREW_STATE_INIT,                                            // 初始化：关所有阀，读液位
	DRAIN_BREW_STATE_OPEN_OUTLET,                                     // 开出液三通阀，等12s
	DRAIN_BREW_STATE_OPEN_CIRC,                                       // 开循环三通阀，等12s
	DRAIN_BREW_STATE_PUMP_ON,                                         // 开循环泵排水，60s超时
	DRAIN_BREW_STATE_LOW_HOLD,                                        // 液位≤100后继续保持2s
	DRAIN_BREW_STATE_DONE,                                            // 完成
	DRAIN_BREW_STATE_ERROR                                            // 故障
} DrainBrewState; /* 为上述定义命名为 DrainBrewState，便于在声明中引用。 */

static DrainBrewState g_drain_brew_state = DRAIN_BREW_STATE_IDLE; /* 定义当前排萃取液状态，切换到 DRAIN_BREW_STATE_IDLE，下一次状态分发按该状态继续。 */
static uint32_t g_drain_brew_start_ms = 0; /* 将g 排液 萃取液 启动 毫秒初始化为零，给模块一个确定的起始值。 */
static uint8_t  g_drain_brew_low_cnt = 0; /* 将g 排液 萃取液 low cnt初始化为零，给模块一个确定的起始值。 */

/* ========================================================================
 * 排水辅助函数
 * ======================================================================== */

static void drain_set_outlet_valve(uint8_t state) /* 按请求控制出液三通阀，软件状态变化时才写 GPIO。 */
{
	if (FLAG_LIQUID_OUTLET_THREE_WAY != state) /* 比较请求与出液三通阀状态镜像，只有状态改变时才再次下发硬件输出。 */
	{
		Liquid_Outlet_Three_Way_Valve_Set(state); /* 将请求状态传给出液三通阀的控制接口，以更新相应输出。 */
	}
}

static void drain_set_circ_valve(uint8_t state) /* 按请求控制循环三通阀，软件状态变化时才写 GPIO。 */
{
	if (FLAG_CIRCULATION_THREE_WAY != state) /* 比较请求与循环三通阀状态镜像，只有状态改变时才再次下发硬件输出。 */
	{
		Circulation_Three_Way_Valve_Set(state); /* 将请求状态传给循环三通阀的控制接口，以更新相应输出。 */
	}
}

static void drain_set_all_closed(void) /* 排液 设置 all closed。 */
{
	coffee_set_pump(0); /* 关闭循环泵，使泵阀按当前流程阶段工作。 */
	coffee_set_inlet(0); /* 关闭进水电磁阀，使泵阀按当前流程阶段工作。 */
	drain_set_outlet_valve(0); /* 关闭出液三通阀，使泵阀按当前流程阶段工作。 */
	drain_set_circ_valve(0); /* 关闭循环三通阀，使泵阀按当前流程阶段工作。 */
}

/* ========================================================================
 * 排废液任务 — Coffee_DrainWaste_Task()
 *
 * 流程：关阀 → 液位≤100直接结束 → 开循环三通12s → 开泵排水(60s超时)
 *       → 连续 3 次读到 ADC ≤ 100 → 泵继续运行 2 s → 关阀完成
 * 初始化用进入本轮时读到的 ADC 缓存，可能是采样关闭前的最后一个结果。
 * ======================================================================== */

void Coffee_DrainWaste_Task(void) /* 推进排废液状态机，按液位条件和超时控制阀泵。 */
{
	uint16_t adc = Liquid_Level_Read(); /* 定义ADC，将ADC 更新为 最近一次液位 ADC 采样结果。 */

	if (FLAG_DRAIN_WASTE == 0) /* 启动标志清零时进入停用分支，让该流程按当前状态完成复位或直接退出。 */
	{
		if (g_drain_waste_state != DRAIN_WASTE_STATE_IDLE) /* 当 「g_drain_waste_state != DRAIN_WASTE_STATE_IDLE」 成立时处理下方分支。 */
		{
			drain_set_all_closed(); /* 执行 drain_set_all_closed 的排液 设置 all closed操作。 */
			FLAG_LIQUID_LEVEL_SAMPLE = 0; /* 清零液位 ADC 采样使能标志，供后续任务判断当前状态。 */
			g_drain_waste_state = DRAIN_WASTE_STATE_IDLE; /* 切换到 DRAIN_WASTE_STATE_IDLE，下一次状态分发按该状态继续。 */
		}
		return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */
	}

	switch (g_drain_waste_state) /* 按当前排废液状态的取值选择对应的处理分支。 */
	{
	case DRAIN_WASTE_STATE_IDLE: /* 进入 DRAIN_WASTE_STATE_IDLE 对应的状态处理分支。 */
		g_drain_waste_state = DRAIN_WASTE_STATE_INIT; /* 切换到 DRAIN_WASTE_STATE_INIT，下一次状态分发按该状态继续。 */
		log_i("DrainWaste: INIT"); /* 以信息级别记录 「"DrainWaste: INIT"」，便于从日志跟踪当前分支。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	case DRAIN_WASTE_STATE_INIT: /* 进入 DRAIN_WASTE_STATE_INIT 对应的状态处理分支。 */
		drain_set_all_closed(); /* 执行 drain_set_all_closed 的排液 设置 all closed操作。 */
		FLAG_LIQUID_LEVEL_SAMPLE = 1; /* 置位液位 ADC 采样使能标志，供后续任务判断当前状态。 */
		if (adc <= DRAIN_LEVEL_LOW_THRESHOLD) /* 液位在排空阈值或更低时，按当前排液阶段执行相应的低液位处理。 */
		{
			log_i("DrainWaste: level already low (ADC=%u), done", adc); /* 以信息级别记录 「"DrainWaste: level already low (ADC=%u), done", adc」，便于从日志跟踪当前分支。 */
			g_drain_waste_state = DRAIN_WASTE_STATE_DONE; /* 切换到 DRAIN_WASTE_STATE_DONE，下一次状态分发按该状态继续。 */
		}
		else /* 前面的条件不满足时执行此分支。 */
		{
			/* 排废液跳过出液三通，直接开循环三通 */
			drain_set_circ_valve(1); /* 开启循环三通阀，使泵阀按当前流程阶段工作。 */
			g_drain_waste_start_ms = delay_millis(); /* 把当前毫秒节拍保存到 g_drain_waste_start_ms，作为之后计算时间差的基准。 */
			g_drain_waste_state = DRAIN_WASTE_STATE_OPEN_CIRC; /* 切换到 DRAIN_WASTE_STATE_OPEN_CIRC，下一次状态分发按该状态继续。 */
			log_i("DrainWaste: OPEN_CIRC, wait 12s"); /* 以信息级别记录 「"DrainWaste: OPEN_CIRC, wait 12s"」，便于从日志跟踪当前分支。 */
		}
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	case DRAIN_WASTE_STATE_OPEN_CIRC: /* 进入 DRAIN_WASTE_STATE_OPEN_CIRC 对应的状态处理分支。 */
		if (delay_expired(g_drain_waste_start_ms, DRAIN_DELAY_12S_MS)) /* 等待阀门切换后留出的 12 秒，再推进排液步骤。 */
		{
			coffee_set_pump(1); /* 开启循环泵，使泵阀按当前流程阶段工作。 */
			g_drain_waste_start_ms = delay_millis(); /* 把当前毫秒节拍保存到 g_drain_waste_start_ms，作为之后计算时间差的基准。 */
			g_drain_waste_low_cnt = 0; /* 将g 排液 废液 low cnt清零，使本轮从初始计数开始。 */
			g_drain_waste_state = DRAIN_WASTE_STATE_PUMP_ON; /* 切换到 DRAIN_WASTE_STATE_PUMP_ON，下一次状态分发按该状态继续。 */
			log_i("DrainWaste: PUMP_ON, 60s timeout"); /* 以信息级别记录 「"DrainWaste: PUMP_ON, 60s timeout"」，便于从日志跟踪当前分支。 */
		}
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	case DRAIN_WASTE_STATE_PUMP_ON: /* 进入 DRAIN_WASTE_STATE_PUMP_ON 对应的状态处理分支。 */
		/* 60s超时 */
		if (delay_expired(g_drain_waste_start_ms, DRAIN_PUMP_TIMEOUT_MS)) /* 检查排液泵运行是否达到 60 秒上限，到期走超时收尾。 */
		{
			drain_set_all_closed(); /* 执行 drain_set_all_closed 的排液 设置 all closed操作。 */
			FLAG_LIQUID_LEVEL_SAMPLE = 0; /* 清零液位 ADC 采样使能标志，供后续任务判断当前状态。 */
			log_e("DrainWaste: pump timeout (60s), abort!"); /* 以错误级别记录 「"DrainWaste: pump timeout (60s), abort!"」，便于从日志跟踪当前分支。 */
			FLAG_DRAIN_WASTE = 0; /* 清零排废液启动标志，供后续任务判断当前状态。 */
			g_drain_waste_state = DRAIN_WASTE_STATE_ERROR; /* 切换到 DRAIN_WASTE_STATE_ERROR，下一次状态分发按该状态继续。 */
			break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
		}

		/* 液位 ≤ 100 连续防抖 */
		if (adc <= DRAIN_LEVEL_LOW_THRESHOLD) /* 液位在排空阈值或更低时，按当前排液阶段执行相应的低液位处理。 */
		{
			g_drain_waste_low_cnt++; /* 将g 排液 废液 low cnt加一，推进处理进度。 */
			if (g_drain_waste_low_cnt >= DRAIN_LEVEL_DEBOUNCE_COUNT) /* 当 「g_drain_waste_low_cnt >= DRAIN_LEVEL_DEBOUNCE_COUNT」 成立时处理下方分支。 */
			{
				g_drain_waste_start_ms = delay_millis(); /* 把当前毫秒节拍保存到 g_drain_waste_start_ms，作为之后计算时间差的基准。 */
				g_drain_waste_low_cnt = 0; /* 将g 排液 废液 low cnt清零，使本轮从初始计数开始。 */
				g_drain_waste_state = DRAIN_WASTE_STATE_LOW_HOLD; /* 切换到 DRAIN_WASTE_STATE_LOW_HOLD，下一次状态分发按该状态继续。 */
				log_i("DrainWaste: LOW_HOLD, keep pump 2s"); /* 以信息级别记录 「"DrainWaste: LOW_HOLD, keep pump 2s"」，便于从日志跟踪当前分支。 */
			}
		}
		else /* 前面的条件不满足时执行此分支。 */
		{
			g_drain_waste_low_cnt = 0; /* 将g 排液 废液 low cnt清零，使本轮从初始计数开始。 */
		}
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	case DRAIN_WASTE_STATE_LOW_HOLD: /* 进入 DRAIN_WASTE_STATE_LOW_HOLD 对应的状态处理分支。 */
		if (delay_expired(g_drain_waste_start_ms, DRAIN_LOW_HOLD_MS)) /* 低液位确认后继续等待保持时长，再结束排液。 */
		{
			coffee_set_pump(0); /* 关闭循环泵，使泵阀按当前流程阶段工作。 */
			g_drain_waste_state = DRAIN_WASTE_STATE_DONE; /* 切换到 DRAIN_WASTE_STATE_DONE，下一次状态分发按该状态继续。 */
			log_i("DrainWaste: DONE, pump stopped"); /* 以信息级别记录 「"DrainWaste: DONE, pump stopped"」，便于从日志跟踪当前分支。 */
		}
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	case DRAIN_WASTE_STATE_DONE: /* 进入 DRAIN_WASTE_STATE_DONE 对应的状态处理分支。 */
		drain_set_all_closed(); /* 执行 drain_set_all_closed 的排液 设置 all closed操作。 */
		FLAG_LIQUID_LEVEL_SAMPLE = 0; /* 清零液位 ADC 采样使能标志，供后续任务判断当前状态。 */
		log_i("DrainWaste: complete, all closed"); /* 以信息级别记录 「"DrainWaste: complete, all closed"」，便于从日志跟踪当前分支。 */
		FLAG_DRAIN_WASTE = 0; /* 清零排废液启动标志，供后续任务判断当前状态。 */
		g_drain_waste_state = DRAIN_WASTE_STATE_IDLE; /* 切换到 DRAIN_WASTE_STATE_IDLE，下一次状态分发按该状态继续。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	case DRAIN_WASTE_STATE_ERROR: /* 进入 DRAIN_WASTE_STATE_ERROR 对应的状态处理分支。 */
		log_e("DrainWaste: ERROR state"); /* 以错误级别记录 「"DrainWaste: ERROR state"」，便于从日志跟踪当前分支。 */
		/* 进入故障时已清启动标志。下一轮通常从函数入口执行收尾并回到 IDLE。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	default: /* 处理没有被列出的其余取值。 */
		g_drain_waste_state = DRAIN_WASTE_STATE_IDLE; /* 切换到 DRAIN_WASTE_STATE_IDLE，下一次状态分发按该状态继续。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
	}
}

/* ========================================================================
 * 排萃取液任务 — Coffee_DrainBrew_Task()
 *
 * 流程（与排废液区别：多一步开出液三通12s）：
 *   关阀 → 液位≤100直接结束 → 开出液三通12s → 开循环三通12s
 *   → 开泵排液（60 秒超时）→ 连续 3 次读到 ADC ≤ 100 → 泵继续运行 2 s → 关阀完成
 * 初始化同样直接使用本轮的 ADC 缓存。
 * ======================================================================== */

void Coffee_DrainBrew_Task(void) /* 推进排萃取液状态机，按液位条件和超时控制阀泵。 */
{
	uint16_t adc = Liquid_Level_Read(); /* 定义ADC，将ADC 更新为 最近一次液位 ADC 采样结果。 */

	if (FLAG_DRAIN_BREW == 0) /* 启动标志清零时进入停用分支，让该流程按当前状态完成复位或直接退出。 */
	{
		if (g_drain_brew_state != DRAIN_BREW_STATE_IDLE) /* 当 「g_drain_brew_state != DRAIN_BREW_STATE_IDLE」 成立时处理下方分支。 */
		{
			drain_set_all_closed(); /* 执行 drain_set_all_closed 的排液 设置 all closed操作。 */
			FLAG_LIQUID_LEVEL_SAMPLE = 0; /* 清零液位 ADC 采样使能标志，供后续任务判断当前状态。 */
			g_drain_brew_state = DRAIN_BREW_STATE_IDLE; /* 切换到 DRAIN_BREW_STATE_IDLE，下一次状态分发按该状态继续。 */
		}
		return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */
	}

	switch (g_drain_brew_state) /* 按当前排萃取液状态的取值选择对应的处理分支。 */
	{
	case DRAIN_BREW_STATE_IDLE: /* 进入 DRAIN_BREW_STATE_IDLE 对应的状态处理分支。 */
		g_drain_brew_state = DRAIN_BREW_STATE_INIT; /* 切换到 DRAIN_BREW_STATE_INIT，下一次状态分发按该状态继续。 */
		log_i("DrainBrew: INIT"); /* 以信息级别记录 「"DrainBrew: INIT"」，便于从日志跟踪当前分支。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	case DRAIN_BREW_STATE_INIT: /* 进入 DRAIN_BREW_STATE_INIT 对应的状态处理分支。 */
		drain_set_all_closed(); /* 执行 drain_set_all_closed 的排液 设置 all closed操作。 */
		FLAG_LIQUID_LEVEL_SAMPLE = 1; /* 置位液位 ADC 采样使能标志，供后续任务判断当前状态。 */
		if (adc <= DRAIN_LEVEL_LOW_THRESHOLD) /* 液位在排空阈值或更低时，按当前排液阶段执行相应的低液位处理。 */
		{
			log_i("DrainBrew: level already low (ADC=%u), done", adc); /* 以信息级别记录 「"DrainBrew: level already low (ADC=%u), done", adc」，便于从日志跟踪当前分支。 */
			g_drain_brew_state = DRAIN_BREW_STATE_DONE; /* 切换到 DRAIN_BREW_STATE_DONE，下一次状态分发按该状态继续。 */
		}
		else /* 前面的条件不满足时执行此分支。 */
		{
			/* 开出液三通阀 */
			drain_set_outlet_valve(1); /* 开启出液三通阀，使泵阀按当前流程阶段工作。 */
			g_drain_brew_start_ms = delay_millis(); /* 把当前毫秒节拍保存到 g_drain_brew_start_ms，作为之后计算时间差的基准。 */
			g_drain_brew_state = DRAIN_BREW_STATE_OPEN_OUTLET; /* 切换到 DRAIN_BREW_STATE_OPEN_OUTLET，下一次状态分发按该状态继续。 */
			log_i("DrainBrew: OPEN_OUTLET, wait 12s"); /* 以信息级别记录 「"DrainBrew: OPEN_OUTLET, wait 12s"」，便于从日志跟踪当前分支。 */
		}
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	case DRAIN_BREW_STATE_OPEN_OUTLET: /* 进入 DRAIN_BREW_STATE_OPEN_OUTLET 对应的状态处理分支。 */
		if (delay_expired(g_drain_brew_start_ms, DRAIN_DELAY_12S_MS)) /* 等待阀门切换后留出的 12 秒，再推进排液步骤。 */
		{
			/* 开循环三通阀 */
			drain_set_circ_valve(1); /* 开启循环三通阀，使泵阀按当前流程阶段工作。 */
			g_drain_brew_start_ms = delay_millis(); /* 把当前毫秒节拍保存到 g_drain_brew_start_ms，作为之后计算时间差的基准。 */
			g_drain_brew_state = DRAIN_BREW_STATE_OPEN_CIRC; /* 切换到 DRAIN_BREW_STATE_OPEN_CIRC，下一次状态分发按该状态继续。 */
			log_i("DrainBrew: OPEN_CIRC, wait 12s"); /* 以信息级别记录 「"DrainBrew: OPEN_CIRC, wait 12s"」，便于从日志跟踪当前分支。 */
		}
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	case DRAIN_BREW_STATE_OPEN_CIRC: /* 进入 DRAIN_BREW_STATE_OPEN_CIRC 对应的状态处理分支。 */
		if (delay_expired(g_drain_brew_start_ms, DRAIN_DELAY_12S_MS)) /* 等待阀门切换后留出的 12 秒，再推进排液步骤。 */
		{
			coffee_set_pump(1); /* 开启循环泵，使泵阀按当前流程阶段工作。 */
			g_drain_brew_start_ms = delay_millis(); /* 把当前毫秒节拍保存到 g_drain_brew_start_ms，作为之后计算时间差的基准。 */
			g_drain_brew_low_cnt = 0; /* 将g 排液 萃取液 low cnt清零，使本轮从初始计数开始。 */
			g_drain_brew_state = DRAIN_BREW_STATE_PUMP_ON; /* 切换到 DRAIN_BREW_STATE_PUMP_ON，下一次状态分发按该状态继续。 */
			log_i("DrainBrew: PUMP_ON, 60s timeout"); /* 以信息级别记录 「"DrainBrew: PUMP_ON, 60s timeout"」，便于从日志跟踪当前分支。 */
		}
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	case DRAIN_BREW_STATE_PUMP_ON: /* 进入 DRAIN_BREW_STATE_PUMP_ON 对应的状态处理分支。 */
		/* 60s超时 */
		if (delay_expired(g_drain_brew_start_ms, DRAIN_PUMP_TIMEOUT_MS)) /* 检查排液泵运行是否达到 60 秒上限，到期走超时收尾。 */
		{
			drain_set_all_closed(); /* 执行 drain_set_all_closed 的排液 设置 all closed操作。 */
			FLAG_LIQUID_LEVEL_SAMPLE = 0; /* 清零液位 ADC 采样使能标志，供后续任务判断当前状态。 */
			log_e("DrainBrew: pump timeout (60s), abort!"); /* 以错误级别记录 「"DrainBrew: pump timeout (60s), abort!"」，便于从日志跟踪当前分支。 */
			FLAG_DRAIN_BREW = 0; /* 清零排萃取液启动标志，供后续任务判断当前状态。 */
			g_drain_brew_state = DRAIN_BREW_STATE_ERROR; /* 切换到 DRAIN_BREW_STATE_ERROR，下一次状态分发按该状态继续。 */
			break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
		}

		/* 液位 ≤ 100 连续防抖 */
		if (adc <= DRAIN_LEVEL_LOW_THRESHOLD) /* 液位在排空阈值或更低时，按当前排液阶段执行相应的低液位处理。 */
		{
			g_drain_brew_low_cnt++; /* 将g 排液 萃取液 low cnt加一，推进处理进度。 */
			if (g_drain_brew_low_cnt >= DRAIN_LEVEL_DEBOUNCE_COUNT) /* 当 「g_drain_brew_low_cnt >= DRAIN_LEVEL_DEBOUNCE_COUNT」 成立时处理下方分支。 */
			{
				g_drain_brew_start_ms = delay_millis(); /* 把当前毫秒节拍保存到 g_drain_brew_start_ms，作为之后计算时间差的基准。 */
				g_drain_brew_low_cnt = 0; /* 将g 排液 萃取液 low cnt清零，使本轮从初始计数开始。 */
				g_drain_brew_state = DRAIN_BREW_STATE_LOW_HOLD; /* 切换到 DRAIN_BREW_STATE_LOW_HOLD，下一次状态分发按该状态继续。 */
				log_i("DrainBrew: LOW_HOLD, keep pump 2s"); /* 以信息级别记录 「"DrainBrew: LOW_HOLD, keep pump 2s"」，便于从日志跟踪当前分支。 */
			}
		}
		else /* 前面的条件不满足时执行此分支。 */
		{
			g_drain_brew_low_cnt = 0; /* 将g 排液 萃取液 low cnt清零，使本轮从初始计数开始。 */
		}
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	case DRAIN_BREW_STATE_LOW_HOLD: /* 进入 DRAIN_BREW_STATE_LOW_HOLD 对应的状态处理分支。 */
		if (delay_expired(g_drain_brew_start_ms, DRAIN_LOW_HOLD_MS)) /* 低液位确认后继续等待保持时长，再结束排液。 */
		{
			coffee_set_pump(0); /* 关闭循环泵，使泵阀按当前流程阶段工作。 */
			g_drain_brew_state = DRAIN_BREW_STATE_DONE; /* 切换到 DRAIN_BREW_STATE_DONE，下一次状态分发按该状态继续。 */
			log_i("DrainBrew: DONE, pump stopped"); /* 以信息级别记录 「"DrainBrew: DONE, pump stopped"」，便于从日志跟踪当前分支。 */
		}
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	case DRAIN_BREW_STATE_DONE: /* 进入 DRAIN_BREW_STATE_DONE 对应的状态处理分支。 */
		drain_set_all_closed(); /* 执行 drain_set_all_closed 的排液 设置 all closed操作。 */
		FLAG_LIQUID_LEVEL_SAMPLE = 0; /* 清零液位 ADC 采样使能标志，供后续任务判断当前状态。 */
		log_i("DrainBrew: complete, all closed"); /* 以信息级别记录 「"DrainBrew: complete, all closed"」，便于从日志跟踪当前分支。 */
		FLAG_DRAIN_BREW = 0; /* 清零排萃取液启动标志，供后续任务判断当前状态。 */
		g_drain_brew_state = DRAIN_BREW_STATE_IDLE; /* 切换到 DRAIN_BREW_STATE_IDLE，下一次状态分发按该状态继续。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	case DRAIN_BREW_STATE_ERROR: /* 进入 DRAIN_BREW_STATE_ERROR 对应的状态处理分支。 */
		log_e("DrainBrew: ERROR state"); /* 以错误级别记录 「"DrainBrew: ERROR state"」，便于从日志跟踪当前分支。 */
		/* 进入故障时已清启动标志。下一轮通常从函数入口执行收尾并回到 IDLE。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	default: /* 处理没有被列出的其余取值。 */
		g_drain_brew_state = DRAIN_BREW_STATE_IDLE; /* 切换到 DRAIN_BREW_STATE_IDLE，下一次状态分发按该状态继续。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
	}
}

/* ========================== 状态机主任务 ========================== */

void Coffee_Task(void)                                                // 每100ms调用一次
{
	uint16_t liquid_level_adc = Liquid_Level_Read(); /* 定义液体 液位 ADC，将液体 液位 ADC 更新为 最近一次液位 ADC 采样结果。 */

	if (FLAG_COFFEE_START == 0)                                       // 未收到启动命令
	{
		if (g_coffee_state != COFFEE_STATE_IDLE) /* 当 「g_coffee_state != COFFEE_STATE_IDLE」 成立时处理下方分支。 */
		{
			Coffee_ResetState(); /* 关闭萃取直接控制的泵阀并重置本轮计时及状态。 */
		}
		return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */
	}

	switch (g_coffee_state) /* 按当前萃取状态的取值选择对应的处理分支。 */
	{
	/* -------------------- IDLE → INIT -------------------- */
	case COFFEE_STATE_IDLE: /* 进入 COFFEE_STATE_IDLE 对应的状态处理分支。 */
		coffee_change_state(COFFEE_STATE_INIT, "COFFEE_STATE_INIT"); /* 更新到 COFFEE_STATE_INIT 并输出状态日志，后续周期从该状态继续推进。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	/* -------------------- INIT → STAGE1_FILL -------------------- */
	case COFFEE_STATE_INIT: /* 进入 COFFEE_STATE_INIT 对应的状态处理分支。 */
		coffee_set_all_closed(); /* 执行 coffee_set_all_closed 的萃取 设置 all closed操作。 */
		FLAG_LIQUID_LEVEL_SAMPLE = 1; /* 置位液位 ADC 采样使能标志，供后续任务判断当前状态。 */
		g_coffee_state_start_ms = delay_millis();                     // 第1段进水计时起点
		g_level_debounce_cnt = 0; /* 将液位连续达标次数清零，使本轮从初始计数开始。 */
		coffee_change_state(COFFEE_STATE_STAGE1_FILL, "COFFEE_STATE_STAGE1_FILL"); /* 更新到 COFFEE_STATE_STAGE1_FILL 并输出状态日志，后续周期从该状态继续推进。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	/* -------------------- 第1段进水：30秒内达到1L -------------------- */
	case COFFEE_STATE_STAGE1_FILL: /* 进入 COFFEE_STATE_STAGE1_FILL 对应的状态处理分支。 */
	{
		/* 30秒超时检查 */
		if (liquid_level_adc < ADC_1L /* 第一段液位尚未达到 1 L 时，再结合下一行的计时条件判断是否超时。 */
		    && delay_expired(g_coffee_state_start_ms, COFFEE_STAGE1_TIMEOUT_MS)) /* 把 「&& delay_expired(g_coffee_state_start_ms, COFFEE_STAGE1_TIMEOUT_MS))」 并入上行的判断，共同决定是否进入分支。 */
		{
			coffee_set_inlet(0); /* 关闭进水电磁阀，使泵阀按当前流程阶段工作。 */
			log_e("Stage1 fill timeout: 30s not reaching 1L (ADC=%u < %u)", liquid_level_adc, ADC_1L); /* 以错误级别记录 「"Stage1 fill timeout: 30s not reaching 1L (ADC=%u < %u)", liquid_level_adc, ADC_1L」，便于从日志跟踪当前分支。 */
			FLAG_COFFEE_START = 0; /* 清零萃取启动标志，供后续任务判断当前状态。 */
			coffee_change_state(COFFEE_STATE_ERROR, "COFFEE_STATE_ERROR"); /* 更新到 COFFEE_STATE_ERROR 并输出状态日志，后续周期从该状态继续推进。 */
			break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
		}

		/* 液位达标防抖：连续 N 次读到 ≥ ADC_1L 才判定到达 */
		if (liquid_level_adc >= ADC_1L) /* 液位达到第一段阈值后才计入连续达标次数，以过滤单次抖动。 */
		{
			g_level_debounce_cnt++; /* 将液位连续达标次数加一，推进处理进度。 */
			if (g_level_debounce_cnt >= COFFEE_LEVEL_DEBOUNCE_COUNT) /* 连续达标次数达到设定值后确认液位稳定，允许切换到下一阶段。 */
			{
				/* 进水阀不关，STAGE2 继续用 */
				g_coffee_state_start_ms = delay_millis();              // 第2段进水计时起点
				g_level_debounce_cnt = 0; /* 将液位连续达标次数清零，使本轮从初始计数开始。 */
				coffee_change_state(COFFEE_STATE_STAGE2_FILL, "COFFEE_STATE_STAGE2_FILL"); /* 更新到 COFFEE_STATE_STAGE2_FILL 并输出状态日志，后续周期从该状态继续推进。 */
				break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
			}
		}
		else /* 前面的条件不满足时执行此分支。 */
		{
			g_level_debounce_cnt = 0;                                  // 不连续达标则重置计数
		}

		/* 保持进水阀开启 */
		coffee_set_inlet(1); /* 开启进水电磁阀，使泵阀按当前流程阶段工作。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
	}

	/* -------------------- 第2段进水：达到目标液量 -------------------- */
	case COFFEE_STATE_STAGE2_FILL: /* 进入 COFFEE_STATE_STAGE2_FILL 对应的状态处理分支。 */
	{
		/* 先把目标升数减 1 后转为整数，再乘 30 秒。小数部分被截去，结果为 0 时补到 30 秒。 */
		uint32_t stage2_timeout = (uint32_t)(g_target_liters - 1.0f) * COFFEE_STAGE2_PER_LITER_MS; /* 先扣掉第一段已进的 1 L，再按剩余整升数计算第二段时间上限。 */
		if (stage2_timeout == 0) stage2_timeout = COFFEE_STAGE2_PER_LITER_MS;  /* 计算为零时给第二段进水保留一个 30 秒时段，避免刚进入就超时。 */ // 兜底

		if (delay_expired(g_coffee_state_start_ms, stage2_timeout)) /* 检查第二段进水是否超过按目标液量计算的上限。 */
		{
			coffee_set_inlet(0); /* 关闭进水电磁阀，使泵阀按当前流程阶段工作。 */
			log_e("Stage2 fill timeout: target=%.1fL, ADC=%u, expected>=%u", /* 开始构造当前分支的日志记录，格式串后的参数在下一行给出。 */
			      (double)g_target_liters, liquid_level_adc, g_target_adc); /* 把浮点量按日志格式提升为 double，并将其余采样及阈值作为格式参数。 */
			FLAG_COFFEE_START = 0; /* 清零萃取启动标志，供后续任务判断当前状态。 */
			coffee_change_state(COFFEE_STATE_ERROR, "COFFEE_STATE_ERROR"); /* 更新到 COFFEE_STATE_ERROR 并输出状态日志，后续周期从该状态继续推进。 */
			break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
		}

		/* 液位达标防抖 */
		if (liquid_level_adc >= g_target_adc) /* 当前采样达到设定液量时累积确认次数，避免单次抖动提前结束进水。 */
		{
			g_level_debounce_cnt++; /* 将液位连续达标次数加一，推进处理进度。 */
			if (g_level_debounce_cnt >= COFFEE_LEVEL_DEBOUNCE_COUNT) /* 连续达标次数达到设定值后确认液位稳定，允许切换到下一阶段。 */
			{
				coffee_set_inlet(0);                                   // 关闭进水电磁阀
				g_level_debounce_cnt = 0; /* 将液位连续达标次数清零，使本轮从初始计数开始。 */
				g_brew_start_ms = delay_millis();                      // 萃取计时起点
				coffee_change_state(COFFEE_STATE_BREW, "COFFEE_STATE_BREW"); /* 更新到 COFFEE_STATE_BREW 并输出状态日志，后续周期从该状态继续推进。 */
				break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
			}
		}
		else /* 前面的条件不满足时执行此分支。 */
		{
			g_level_debounce_cnt = 0; /* 将液位连续达标次数清零，使本轮从初始计数开始。 */
		}

		/* 保持进水阀开启 */
		coffee_set_inlet(1); /* 开启进水电磁阀，使泵阀按当前流程阶段工作。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
	}

	/* -------------------- 萃取阶段：开循环泵，按设定时间计时 -------------------- */
	case COFFEE_STATE_BREW: /* 进入 COFFEE_STATE_BREW 对应的状态处理分支。 */
	{
		uint32_t brew_timeout_ms = (uint32_t)g_brew_minutes * 60000UL; // 分钟转毫秒

		/* 液位异常保护：液位低于 (N-1)L 则报错 */
		if (liquid_level_adc < g_protect_adc) /* 萃取中液位低于保护阈值时进入故障处理，停止继续萃取。 */
		{
			g_level_debounce_cnt++; /* 将液位连续达标次数加一，推进处理进度。 */
			if (g_level_debounce_cnt >= COFFEE_LEVEL_DEBOUNCE_COUNT) /* 连续达标次数达到设定值后确认液位稳定，允许切换到下一阶段。 */
			{
				coffee_set_pump(0); /* 关闭循环泵，使泵阀按当前流程阶段工作。 */
				log_e("Brew error: liquid level too low, ADC=%u < protect=%u (%.1f-1=%.1f L)", /* 开始构造当前分支的日志记录，格式串后的参数在下一行给出。 */
				      liquid_level_adc, g_protect_adc, (double)g_target_liters, (double)(g_target_liters - 1.0f)); /* 用 「liquid_level_adc, g_protect_adc, (double)g_target_liters, (double)(g_target_liters - 1.0f))」 补足上行的表达式、缓冲区内容或接口参数。 */
				FLAG_COFFEE_START = 0; /* 清零萃取启动标志，供后续任务判断当前状态。 */
				g_level_debounce_cnt = 0; /* 将液位连续达标次数清零，使本轮从初始计数开始。 */
				coffee_change_state(COFFEE_STATE_ERROR, "COFFEE_STATE_ERROR"); /* 更新到 COFFEE_STATE_ERROR 并输出状态日志，后续周期从该状态继续推进。 */
				break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
			}
		}
		else /* 前面的条件不满足时执行此分支。 */
		{
			g_level_debounce_cnt = 0; /* 将液位连续达标次数清零，使本轮从初始计数开始。 */
		}

		/* 萃取时间到 */
		if (delay_expired(g_brew_start_ms, brew_timeout_ms)) /* 累计萃取时间达到设定时长时进入收尾。 */
		{
			coffee_set_pump(0);                                        // 停止循环泵
			log_i("Brew finished: %u minutes elapsed", g_brew_minutes); /* 以信息级别记录 「"Brew finished: %u minutes elapsed", g_brew_minutes」，便于从日志跟踪当前分支。 */
			coffee_change_state(COFFEE_STATE_DRAIN_DECISION, "COFFEE_STATE_DRAIN_DECISION"); /* 更新到 COFFEE_STATE_DRAIN_DECISION 并输出状态日志，后续周期从该状态继续推进。 */
			break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
		}

		/* 保持循环泵运行 */
		coffee_set_pump(1); /* 开启循环泵，使泵阀按当前流程阶段工作。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
	}

	/* -------------------- 排水决策 -------------------- */
	case COFFEE_STATE_DRAIN_DECISION: /* 进入 COFFEE_STATE_DRAIN_DECISION 对应的状态处理分支。 */
		if (g_drain_mode == 1)                                        // 手动排水
		{
			log_i("Manual drain mode: process stopped, extraction complete."); /* 以信息级别记录 「"Manual drain mode: process stopped, extraction complete."」，便于从日志跟踪当前分支。 */
			FLAG_COFFEE_START = 0; /* 清零萃取启动标志，供后续任务判断当前状态。 */
			coffee_change_state(COFFEE_STATE_DONE, "COFFEE_STATE_DONE"); /* 更新到 COFFEE_STATE_DONE 并输出状态日志，后续周期从该状态继续推进。 */
		}
		else                                                           // 自动排水：触发排萃取液
		{
			log_i("Auto drain mode: triggering drain brew task."); /* 以信息级别记录 「"Auto drain mode: triggering drain brew task."」，便于从日志跟踪当前分支。 */
			FLAG_DRAIN_BREW = 1; /* 置位排萃取液启动标志，供后续任务判断当前状态。 */
			FLAG_COFFEE_START = 0; /* 清零萃取启动标志，供后续任务判断当前状态。 */
			coffee_change_state(COFFEE_STATE_DONE, "COFFEE_STATE_DONE"); /* 更新到 COFFEE_STATE_DONE 并输出状态日志，后续周期从该状态继续推进。 */
		}
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	/* 历史排液状态，若进入直接转 DONE。正常自动排液由 Coffee_DrainBrew_Task 执行。 */
	case COFFEE_STATE_DRAIN: /* 进入 COFFEE_STATE_DRAIN 对应的状态处理分支。 */
		coffee_change_state(COFFEE_STATE_DONE, "COFFEE_STATE_DONE"); /* 更新到 COFFEE_STATE_DONE 并输出状态日志，后续周期从该状态继续推进。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	/* 正常决策已清 FLAG_COFFEE_START，下一轮通常由函数入口复位。 */
	case COFFEE_STATE_DONE: /* 进入 COFFEE_STATE_DONE 对应的状态处理分支。 */
		FLAG_LIQUID_LEVEL_SAMPLE = 0; /* 清零液位 ADC 采样使能标志，供后续任务判断当前状态。 */
		log_i("Extraction process complete."); /* 以信息级别记录 「"Extraction process complete."」，便于从日志跟踪当前分支。 */
		FLAG_COFFEE_START = 0; /* 清零萃取启动标志，供后续任务判断当前状态。 */
		coffee_change_state(COFFEE_STATE_IDLE, "COFFEE_STATE_IDLE"); /* 更新到 COFFEE_STATE_IDLE 并输出状态日志，后续周期从该状态继续推进。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	/* -------------------- 故障状态 -------------------- */
	case COFFEE_STATE_ERROR: /* 进入 COFFEE_STATE_ERROR 对应的状态处理分支。 */
		coffee_set_all_closed(); /* 执行 coffee_set_all_closed 的萃取 设置 all closed操作。 */
		FLAG_LIQUID_LEVEL_SAMPLE = 0; /* 清零液位 ADC 采样使能标志，供后续任务判断当前状态。 */
		log_e("Extraction process error, all valves/pumps closed."); /* 以错误级别记录 「"Extraction process error, all valves/pumps closed."」，便于从日志跟踪当前分支。 */
		// 故障转入此状态时已清启动标志，下一轮通常由函数入口复位
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */

	default: /* 处理没有被列出的其余取值。 */
		break; /* 结束当前状态分支，本轮其余状态留待后续调用处理。 */
	}
}
