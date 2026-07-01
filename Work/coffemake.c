#include "stm32f10x.h"
#include "delay.h"
#include "GPIO.h"
#include "flag.h"                                                   // 引入流程标志位定义
#include "coffemake.h"                                              // 引入萃取流程接口声明
#include "elog.h"                                                   // EasyLogger 统一日志接口
#include <stdlib.h>                                                 // atof 字符串转浮点

#define LOG_TAG                         "coffee"

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
#define SCREEN_ID_COFFEE                   0x0003U                   // 萃取画面ID

/* ========================== 状态机 ========================== */
typedef enum
{
	COFFEE_STATE_IDLE = 0,                                            // 空闲状态，等待启动
	COFFEE_STATE_INIT,                                                // 初始化状态，关闭阀泵并开启液位采样
	COFFEE_STATE_STAGE1_FILL,                                         // 第1段进水：30秒内必须达到1L
	COFFEE_STATE_STAGE2_FILL,                                         // 第2段进水：继续进水到目标液位
	COFFEE_STATE_BREW,                                                // 萃取阶段：开循环泵，按设定时间计时
	COFFEE_STATE_DRAIN_DECISION,                                      // 排水决策：判断自动/手动
	COFFEE_STATE_DRAIN,                                               // 自动排水阶段
	COFFEE_STATE_DONE,                                                // 萃取完成
	COFFEE_STATE_ERROR                                                // 故障状态
} CoffeeState;

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
	if (FLAG_WATER_INLET_VALVE != state)
	{
		water_inlet_solenoid_valve_Set(state);
	}
}

static void coffee_set_pump(uint8_t state)                           // 设置循环泵且避免重复下发
{
	if (FLAG_CIRCULATION_PUMP != state)
	{
		circulation_pump_Set(state);
	}
}

static void coffee_set_all_closed(void)                              // 关闭萃取涉及的所有阀和泵
{
	coffee_set_pump(0);
	coffee_set_inlet(0);
}

static void coffee_change_state(CoffeeState next_state, const char *message)
{
	g_coffee_state = next_state;
	log_i("%s", message);
}

/* ========================== 屏幕参数接收接口 ========================== */

/*
 * 由 cmd_process.c 在收到萃取页面文本控件通知时调用
 * control_id:
 *   0x0001 — 液量设定（字符串，如 "3.5"）
 *   0x0002 — 时间设定（字符串，如 "10"）
 */
void Coffee_SetParam(uint16_t control_id, uint8_t *str)
{
	if (str == 0) return;

	switch (control_id)
	{
	case 0x0001:                                                      // 液量设定
	{
		float val = (float)atof((const char *)str);
		if (val < 1.0f) val = 1.0f;
		if (val > 5.0f) val = 5.0f;
		g_target_liters = val;
		g_target_adc = (uint16_t)(ADC_0L + (uint32_t)(val * (float)ADC_PER_LITER));
		// 保护阈值 = (设定升数 - 1) 对应的ADC
		if (val > 1.0f)
		{
			g_protect_adc = (uint16_t)(ADC_0L + (uint32_t)((val - 1.0f) * (float)ADC_PER_LITER));
		}
		else
		{
			g_protect_adc = ADC_0L;                                   // 设定1L时，保护阈值即为0L
		}
		log_i("Target set: %.1f L -> ADC=%u, protect ADC=%u", (double)val, g_target_adc, g_protect_adc);
		break;
	}
	case 0x0002:                                                      // 萃取时间设定
	{
		uint32_t val = (uint32_t)atoi((const char *)str);
		if (val < 1)  val = 1;
		if (val > 60) val = 60;
		g_brew_minutes = (uint16_t)val;
		log_i("Brew time set: %u min", g_brew_minutes);
		break;
	}
	default:
		break;
	}
}

/*
 * 由 cmd_process.c 在收到萃取页面选择器通知时调用
 * control_id 0x0003, item: 0=自动排水, 1=手动排水
 */
void Coffee_SetDrainMode(uint8_t item)
{
	g_drain_mode = item;
	log_i("Drain mode set: %s", (item == 0) ? "AUTO" : "MANUAL");
}

/* ========================== 外部接口 ========================== */

void Coffee_ResetState(void)                                          // 复位萃取流程
{
	FLAG_LIQUID_LEVEL_SAMPLE = 0;
	coffee_set_all_closed();
	g_level_debounce_cnt = 0;
	coffee_change_state(COFFEE_STATE_IDLE, "COFFEE_STATE_IDLE");
	g_coffee_state_start_ms = 0;
	g_brew_start_ms = 0;
}

/* ========================================================================
 * 排水流程 — 通用宏定义
 * ======================================================================== */

#define DRAIN_LEVEL_LOW_THRESHOLD           100U                      // 排水结束液位阈值（ADC）
#define DRAIN_LEVEL_DEBOUNCE_COUNT          3U                        // 液位低于阈值连续确认次数
#define DRAIN_DELAY_12S_MS                  12000UL                   // 开阀后等待12秒
#define DRAIN_PUMP_TIMEOUT_MS               60000UL                   // 循环泵排水60秒超时
#define DRAIN_LOW_HOLD_MS                   2000UL                    // 液位低于阈值后继续保持2秒

/* ========================================================================
 * 排废液状态机
 * ======================================================================== */

typedef enum
{
	DRAIN_WASTE_STATE_IDLE = 0,                                       // 空闲
	DRAIN_WASTE_STATE_INIT,                                           // 初始化：关所有阀，读液位
	DRAIN_WASTE_STATE_OPEN_CIRC,                                      // 开循环三通阀，等12s（跳过出液三通）
	DRAIN_WASTE_STATE_PUMP_ON,                                        // 开循环泵排水，60s超时
	DRAIN_WASTE_STATE_LOW_HOLD,                                       // 液位≤100后继续保持2s
	DRAIN_WASTE_STATE_DONE,                                           // 完成
	DRAIN_WASTE_STATE_ERROR                                           // 故障
} DrainWasteState;

static DrainWasteState g_drain_waste_state = DRAIN_WASTE_STATE_IDLE;
static uint32_t g_drain_waste_start_ms = 0;
static uint8_t  g_drain_waste_low_cnt = 0;

/* ========================================================================
 * 排萃取液状态机
 * ======================================================================== */

typedef enum
{
	DRAIN_BREW_STATE_IDLE = 0,                                        // 空闲
	DRAIN_BREW_STATE_INIT,                                            // 初始化：关所有阀，读液位
	DRAIN_BREW_STATE_OPEN_OUTLET,                                     // 开出液三通阀，等12s
	DRAIN_BREW_STATE_OPEN_CIRC,                                       // 开循环三通阀，等12s
	DRAIN_BREW_STATE_PUMP_ON,                                         // 开循环泵排水，60s超时
	DRAIN_BREW_STATE_LOW_HOLD,                                        // 液位≤100后继续保持2s
	DRAIN_BREW_STATE_DONE,                                            // 完成
	DRAIN_BREW_STATE_ERROR                                            // 故障
} DrainBrewState;

static DrainBrewState g_drain_brew_state = DRAIN_BREW_STATE_IDLE;
static uint32_t g_drain_brew_start_ms = 0;
static uint8_t  g_drain_brew_low_cnt = 0;

/* ========================================================================
 * 排水辅助函数
 * ======================================================================== */

static void drain_set_outlet_valve(uint8_t state)
{
	if (FLAG_LIQUID_OUTLET_THREE_WAY != state)
	{
		Liquid_Outlet_Three_Way_Valve_Set(state);
	}
}

static void drain_set_circ_valve(uint8_t state)
{
	if (FLAG_CIRCULATION_THREE_WAY != state)
	{
		Circulation_Three_Way_Valve_Set(state);
	}
}

static void drain_set_all_closed(void)
{
	coffee_set_pump(0);
	coffee_set_inlet(0);
	drain_set_outlet_valve(0);
	drain_set_circ_valve(0);
}

/* ========================================================================
 * 排废液任务 — Coffee_DrainWaste_Task()
 *
 * 流程：关阀 → 液位≤100直接结束 → 开循环三通12s → 开泵排水(60s超时)
 *       → 液位≤100保持2s → 关阀完成
 * ======================================================================== */

void Coffee_DrainWaste_Task(void)
{
	uint16_t adc = Liquid_Level_Read();

	if (FLAG_DRAIN_WASTE == 0)
	{
		if (g_drain_waste_state != DRAIN_WASTE_STATE_IDLE)
		{
			drain_set_all_closed();
			FLAG_LIQUID_LEVEL_SAMPLE = 0;
			g_drain_waste_state = DRAIN_WASTE_STATE_IDLE;
		}
		return;
	}

	switch (g_drain_waste_state)
	{
	case DRAIN_WASTE_STATE_IDLE:
		g_drain_waste_state = DRAIN_WASTE_STATE_INIT;
		log_i("DrainWaste: INIT");
		break;

	case DRAIN_WASTE_STATE_INIT:
		drain_set_all_closed();
		FLAG_LIQUID_LEVEL_SAMPLE = 1;
		if (adc <= DRAIN_LEVEL_LOW_THRESHOLD)
		{
			log_i("DrainWaste: level already low (ADC=%u), done", adc);
			g_drain_waste_state = DRAIN_WASTE_STATE_DONE;
		}
		else
		{
			/* 排废液跳过出液三通，直接开循环三通 */
			drain_set_circ_valve(1);
			g_drain_waste_start_ms = delay_millis();
			g_drain_waste_state = DRAIN_WASTE_STATE_OPEN_CIRC;
			log_i("DrainWaste: OPEN_CIRC, wait 12s");
		}
		break;

	case DRAIN_WASTE_STATE_OPEN_CIRC:
		if (delay_expired(g_drain_waste_start_ms, DRAIN_DELAY_12S_MS))
		{
			coffee_set_pump(1);
			g_drain_waste_start_ms = delay_millis();
			g_drain_waste_low_cnt = 0;
			g_drain_waste_state = DRAIN_WASTE_STATE_PUMP_ON;
			log_i("DrainWaste: PUMP_ON, 60s timeout");
		}
		break;

	case DRAIN_WASTE_STATE_PUMP_ON:
		/* 60s超时 */
		if (delay_expired(g_drain_waste_start_ms, DRAIN_PUMP_TIMEOUT_MS))
		{
			drain_set_all_closed();
			FLAG_LIQUID_LEVEL_SAMPLE = 0;
			log_e("DrainWaste: pump timeout (60s), abort!");
			FLAG_DRAIN_WASTE = 0;
			g_drain_waste_state = DRAIN_WASTE_STATE_ERROR;
			break;
		}

		/* 液位 ≤ 100 连续防抖 */
		if (adc <= DRAIN_LEVEL_LOW_THRESHOLD)
		{
			g_drain_waste_low_cnt++;
			if (g_drain_waste_low_cnt >= DRAIN_LEVEL_DEBOUNCE_COUNT)
			{
				g_drain_waste_start_ms = delay_millis();
				g_drain_waste_low_cnt = 0;
				g_drain_waste_state = DRAIN_WASTE_STATE_LOW_HOLD;
				log_i("DrainWaste: LOW_HOLD, keep pump 2s");
			}
		}
		else
		{
			g_drain_waste_low_cnt = 0;
		}
		break;

	case DRAIN_WASTE_STATE_LOW_HOLD:
		if (delay_expired(g_drain_waste_start_ms, DRAIN_LOW_HOLD_MS))
		{
			coffee_set_pump(0);
			g_drain_waste_state = DRAIN_WASTE_STATE_DONE;
			log_i("DrainWaste: DONE, pump stopped");
		}
		break;

	case DRAIN_WASTE_STATE_DONE:
		drain_set_all_closed();
		FLAG_LIQUID_LEVEL_SAMPLE = 0;
		log_i("DrainWaste: complete, all closed");
		FLAG_DRAIN_WASTE = 0;
		g_drain_waste_state = DRAIN_WASTE_STATE_IDLE;
		break;

	case DRAIN_WASTE_STATE_ERROR:
		log_e("DrainWaste: ERROR state");
		/* 由外部清零 FLAG 后回到 IDLE */
		break;

	default:
		g_drain_waste_state = DRAIN_WASTE_STATE_IDLE;
		break;
	}
}

/* ========================================================================
 * 排萃取液任务 — Coffee_DrainBrew_Task()
 *
 * 流程（与排废液区别：多一步开出液三通12s）：
 *   关阀 → 液位≤100直接结束 → 开出液三通12s → 开循环三通12s
 *   → 开泵排水(60s超时) → 液位≤100保持2s → 关阀完成
 * ======================================================================== */

void Coffee_DrainBrew_Task(void)
{
	uint16_t adc = Liquid_Level_Read();

	if (FLAG_DRAIN_BREW == 0)
	{
		if (g_drain_brew_state != DRAIN_BREW_STATE_IDLE)
		{
			drain_set_all_closed();
			FLAG_LIQUID_LEVEL_SAMPLE = 0;
			g_drain_brew_state = DRAIN_BREW_STATE_IDLE;
		}
		return;
	}

	switch (g_drain_brew_state)
	{
	case DRAIN_BREW_STATE_IDLE:
		g_drain_brew_state = DRAIN_BREW_STATE_INIT;
		log_i("DrainBrew: INIT");
		break;

	case DRAIN_BREW_STATE_INIT:
		drain_set_all_closed();
		FLAG_LIQUID_LEVEL_SAMPLE = 1;
		if (adc <= DRAIN_LEVEL_LOW_THRESHOLD)
		{
			log_i("DrainBrew: level already low (ADC=%u), done", adc);
			g_drain_brew_state = DRAIN_BREW_STATE_DONE;
		}
		else
		{
			/* 开出液三通阀 */
			drain_set_outlet_valve(1);
			g_drain_brew_start_ms = delay_millis();
			g_drain_brew_state = DRAIN_BREW_STATE_OPEN_OUTLET;
			log_i("DrainBrew: OPEN_OUTLET, wait 12s");
		}
		break;

	case DRAIN_BREW_STATE_OPEN_OUTLET:
		if (delay_expired(g_drain_brew_start_ms, DRAIN_DELAY_12S_MS))
		{
			/* 开循环三通阀 */
			drain_set_circ_valve(1);
			g_drain_brew_start_ms = delay_millis();
			g_drain_brew_state = DRAIN_BREW_STATE_OPEN_CIRC;
			log_i("DrainBrew: OPEN_CIRC, wait 12s");
		}
		break;

	case DRAIN_BREW_STATE_OPEN_CIRC:
		if (delay_expired(g_drain_brew_start_ms, DRAIN_DELAY_12S_MS))
		{
			coffee_set_pump(1);
			g_drain_brew_start_ms = delay_millis();
			g_drain_brew_low_cnt = 0;
			g_drain_brew_state = DRAIN_BREW_STATE_PUMP_ON;
			log_i("DrainBrew: PUMP_ON, 60s timeout");
		}
		break;

	case DRAIN_BREW_STATE_PUMP_ON:
		/* 60s超时 */
		if (delay_expired(g_drain_brew_start_ms, DRAIN_PUMP_TIMEOUT_MS))
		{
			drain_set_all_closed();
			FLAG_LIQUID_LEVEL_SAMPLE = 0;
			log_e("DrainBrew: pump timeout (60s), abort!");
			FLAG_DRAIN_BREW = 0;
			g_drain_brew_state = DRAIN_BREW_STATE_ERROR;
			break;
		}

		/* 液位 ≤ 100 连续防抖 */
		if (adc <= DRAIN_LEVEL_LOW_THRESHOLD)
		{
			g_drain_brew_low_cnt++;
			if (g_drain_brew_low_cnt >= DRAIN_LEVEL_DEBOUNCE_COUNT)
			{
				g_drain_brew_start_ms = delay_millis();
				g_drain_brew_low_cnt = 0;
				g_drain_brew_state = DRAIN_BREW_STATE_LOW_HOLD;
				log_i("DrainBrew: LOW_HOLD, keep pump 2s");
			}
		}
		else
		{
			g_drain_brew_low_cnt = 0;
		}
		break;

	case DRAIN_BREW_STATE_LOW_HOLD:
		if (delay_expired(g_drain_brew_start_ms, DRAIN_LOW_HOLD_MS))
		{
			coffee_set_pump(0);
			g_drain_brew_state = DRAIN_BREW_STATE_DONE;
			log_i("DrainBrew: DONE, pump stopped");
		}
		break;

	case DRAIN_BREW_STATE_DONE:
		drain_set_all_closed();
		FLAG_LIQUID_LEVEL_SAMPLE = 0;
		log_i("DrainBrew: complete, all closed");
		FLAG_DRAIN_BREW = 0;
		g_drain_brew_state = DRAIN_BREW_STATE_IDLE;
		break;

	case DRAIN_BREW_STATE_ERROR:
		log_e("DrainBrew: ERROR state");
		/* 由外部清零 FLAG 后回到 IDLE */
		break;

	default:
		g_drain_brew_state = DRAIN_BREW_STATE_IDLE;
		break;
	}
}

/* ========================== 状态机主任务 ========================== */

void Coffee_Task(void)                                                // 每100ms调用一次
{
	uint16_t liquid_level_adc = Liquid_Level_Read();

	if (FLAG_COFFEE_START == 0)                                       // 未收到启动命令
	{
		if (g_coffee_state != COFFEE_STATE_IDLE)
		{
			Coffee_ResetState();
		}
		return;
	}

	switch (g_coffee_state)
	{
	/* -------------------- IDLE → INIT -------------------- */
	case COFFEE_STATE_IDLE:
		coffee_change_state(COFFEE_STATE_INIT, "COFFEE_STATE_INIT");
		break;

	/* -------------------- INIT → STAGE1_FILL -------------------- */
	case COFFEE_STATE_INIT:
		coffee_set_all_closed();
		FLAG_LIQUID_LEVEL_SAMPLE = 1;
		g_coffee_state_start_ms = delay_millis();                     // 第1段进水计时起点
		g_level_debounce_cnt = 0;
		coffee_change_state(COFFEE_STATE_STAGE1_FILL, "COFFEE_STATE_STAGE1_FILL");
		break;

	/* -------------------- 第1段进水：30秒内达到1L -------------------- */
	case COFFEE_STATE_STAGE1_FILL:
	{
		/* 30秒超时检查 */
		if (liquid_level_adc < ADC_1L
		    && delay_expired(g_coffee_state_start_ms, COFFEE_STAGE1_TIMEOUT_MS))
		{
			coffee_set_inlet(0);
			log_e("Stage1 fill timeout: 30s not reaching 1L (ADC=%u < %u)", liquid_level_adc, ADC_1L);
			FLAG_COFFEE_START = 0;
			coffee_change_state(COFFEE_STATE_ERROR, "COFFEE_STATE_ERROR");
			break;
		}

		/* 液位达标防抖：连续 N 次读到 ≥ ADC_1L 才判定到达 */
		if (liquid_level_adc >= ADC_1L)
		{
			g_level_debounce_cnt++;
			if (g_level_debounce_cnt >= COFFEE_LEVEL_DEBOUNCE_COUNT)
			{
				/* 进水阀不关，STAGE2 继续用 */
				g_coffee_state_start_ms = delay_millis();              // 第2段进水计时起点
				g_level_debounce_cnt = 0;
				coffee_change_state(COFFEE_STATE_STAGE2_FILL, "COFFEE_STATE_STAGE2_FILL");
				break;
			}
		}
		else
		{
			g_level_debounce_cnt = 0;                                  // 不连续达标则重置计数
		}

		/* 保持进水阀开启 */
		coffee_set_inlet(1);
		break;
	}

	/* -------------------- 第2段进水：达到目标液量 -------------------- */
	case COFFEE_STATE_STAGE2_FILL:
	{
		/* 超时计算：(target - 1) * 30s */
		uint32_t stage2_timeout = (uint32_t)(g_target_liters - 1.0f) * COFFEE_STAGE2_PER_LITER_MS;
		if (stage2_timeout == 0) stage2_timeout = COFFEE_STAGE2_PER_LITER_MS; // 兜底

		if (delay_expired(g_coffee_state_start_ms, stage2_timeout))
		{
			coffee_set_inlet(0);
			log_e("Stage2 fill timeout: target=%.1fL, ADC=%u, expected>=%u",
			      (double)g_target_liters, liquid_level_adc, g_target_adc);
			FLAG_COFFEE_START = 0;
			coffee_change_state(COFFEE_STATE_ERROR, "COFFEE_STATE_ERROR");
			break;
		}

		/* 液位达标防抖 */
		if (liquid_level_adc >= g_target_adc)
		{
			g_level_debounce_cnt++;
			if (g_level_debounce_cnt >= COFFEE_LEVEL_DEBOUNCE_COUNT)
			{
				coffee_set_inlet(0);                                   // 关闭进水电磁阀
				g_level_debounce_cnt = 0;
				g_brew_start_ms = delay_millis();                      // 萃取计时起点
				coffee_change_state(COFFEE_STATE_BREW, "COFFEE_STATE_BREW");
				break;
			}
		}
		else
		{
			g_level_debounce_cnt = 0;
		}

		/* 保持进水阀开启 */
		coffee_set_inlet(1);
		break;
	}

	/* -------------------- 萃取阶段：开循环泵，按设定时间计时 -------------------- */
	case COFFEE_STATE_BREW:
	{
		uint32_t brew_timeout_ms = (uint32_t)g_brew_minutes * 60000UL; // 分钟转毫秒

		/* 液位异常保护：液位低于 (N-1)L 则报错 */
		if (liquid_level_adc < g_protect_adc)
		{
			g_level_debounce_cnt++;
			if (g_level_debounce_cnt >= COFFEE_LEVEL_DEBOUNCE_COUNT)
			{
				coffee_set_pump(0);
				log_e("Brew error: liquid level too low, ADC=%u < protect=%u (%.1f-1=%.1f L)",
				      liquid_level_adc, g_protect_adc, (double)g_target_liters, (double)(g_target_liters - 1.0f));
				FLAG_COFFEE_START = 0;
				g_level_debounce_cnt = 0;
				coffee_change_state(COFFEE_STATE_ERROR, "COFFEE_STATE_ERROR");
				break;
			}
		}
		else
		{
			g_level_debounce_cnt = 0;
		}

		/* 萃取时间到 */
		if (delay_expired(g_brew_start_ms, brew_timeout_ms))
		{
			coffee_set_pump(0);                                        // 停止循环泵
			log_i("Brew finished: %u minutes elapsed", g_brew_minutes);
			coffee_change_state(COFFEE_STATE_DRAIN_DECISION, "COFFEE_STATE_DRAIN_DECISION");
			break;
		}

		/* 保持循环泵运行 */
		coffee_set_pump(1);
		break;
	}

	/* -------------------- 排水决策 -------------------- */
	case COFFEE_STATE_DRAIN_DECISION:
		if (g_drain_mode == 1)                                        // 手动排水
		{
			log_i("Manual drain mode: process stopped, extraction complete.");
			FLAG_COFFEE_START = 0;
			coffee_change_state(COFFEE_STATE_DONE, "COFFEE_STATE_DONE");
		}
		else                                                           // 自动排水：触发排萃取液
		{
			log_i("Auto drain mode: triggering drain brew task.");
			FLAG_DRAIN_BREW = 1;
			FLAG_COFFEE_START = 0;
			coffee_change_state(COFFEE_STATE_DONE, "COFFEE_STATE_DONE");
		}
		break;

	/* -------------------- 排水阶段：不再使用，保留case避免编译警告 -------------------- */
	case COFFEE_STATE_DRAIN:
		/* 排水已由独立的 Coffee_DrainBrew_Task 处理，此状态不再使用 */
		coffee_change_state(COFFEE_STATE_DONE, "COFFEE_STATE_DONE");
		break;

	/* -------------------- 萃取完成 -------------------- */
	case COFFEE_STATE_DONE:
		FLAG_LIQUID_LEVEL_SAMPLE = 0;
		log_i("Extraction process complete.");
		FLAG_COFFEE_START = 0;
		coffee_change_state(COFFEE_STATE_IDLE, "COFFEE_STATE_IDLE");
		break;

	/* -------------------- 故障状态 -------------------- */
	case COFFEE_STATE_ERROR:
		coffee_set_all_closed();
		FLAG_LIQUID_LEVEL_SAMPLE = 0;
		log_e("Extraction process error, all valves/pumps closed.");
		// 停留在 ERROR 状态，直到 FLAG_COFFEE_START 被外部清零后由 IDLE 分支复位
		break;

	default:
		break;
	}
}
