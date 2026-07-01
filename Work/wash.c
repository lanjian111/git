#include "stm32f10x.h"
#include "delay.h"
#include "LED.h"
#include "GPIO.h"
#include "flag.h"                                                   // 引入流程标志位定义
#include "wash.h"                                                   // 引入清洗流程接口声明
#include "coffemake.h"                                              // 引入排水函数声明
#include "elog.h"                                                   // EasyLogger 统一日志接口

#define LOG_TAG                         "wash"

#define WASH_LEVEL_LOW_THRESHOLD              100U                    // 排液结束液位阈值
#define WASH_LEVEL_PUMP_ON_THRESHOLD          1000U                   // 允许开启循环泵的液位阈值
#define WASH_LEVEL_STAGE1_TARGET              2000U                   // 第一段清洗目标液位
#define WASH_LEVEL_STAGE2_TARGET              3000U                   // 第二段清洗目标液位

#define WASH_SOAK_TIME_MS                     60000UL                 // 浸泡/循环保持时间60秒
#define WASH_SOAK_TO_CIRC_DELAY_MS            12000UL                 // 浸泡结束后等待12秒再开循环三通
#define WASH_CIRC_TO_PUMP_DELAY_MS            12000UL                 // 开循环三通后等待12秒再开泵
#define WASH_LOW_LEVEL_HOLD_MS                2000UL                  // 液位低于阈值后继续保持2秒
#define WASH_REFILL_DELAY_MS                  12000UL                 // 关闭阀门后等待12秒再进入下一段
#define WASH_FILL_TIMEOUT_MS                  30000UL                 // 第一段进水30秒未达到1000则超时
#define WASH_FILL_TOTAL_TIMEOUT_MS            120000UL                // 整个进水阶段总超时120秒（含达到1000后到2000的过程）
#define WASH_DRAIN_TIMEOUT_MS                 60000UL                 // 排液最长60秒，超时则终止清洗
#define WASH_LEVEL_DEBOUNCE_COUNT             3U                      // 液位阈值连续确认次数（每次100ms）

typedef enum
{
	WASH_STATE_IDLE = 0,                                              // 空闲状态，等待启动
	WASH_STATE_INIT,                                                  // 初始化状态，关闭阀泵并开启液位采样
	WASH_STATE_STAGE1_FILL,                                           // 第一段进水到2000
	WASH_STATE_STAGE1_SOAK,                                           // 第一段到位后保持60秒
	WASH_STATE_STAGE1_OPEN_CIRC_DELAY,                                // 第一段浸泡结束后等待12秒再开循环三通
	WASH_STATE_STAGE1_START_PUMP_DELAY,                               // 第一段开循环三通后等待12秒再开泵
	WASH_STATE_STAGE1_DRAIN,                                          // 第一段排液直到液位低于100
	WASH_STATE_STAGE1_LOW_LEVEL_HOLD,                                 // 第一段低液位后继续保持2秒
	WASH_STATE_STAGE1_REFILL_DELAY,                                   // 第一段结束后等待12秒进入第二段
	WASH_STATE_STAGE2_FILL,                                           // 第二段进水到3000
	WASH_STATE_STAGE2_SOAK,                                           // 第二段到位后保持60秒
	WASH_STATE_STAGE2_OPEN_CIRC_DELAY,                                // 第二段浸泡结束后等待12秒再开循环三通
	WASH_STATE_STAGE2_START_PUMP_DELAY,                               // 第二段开循环三通后等待12秒再开泵
	WASH_STATE_STAGE2_DRAIN,                                          // 第二段排液直到液位低于100
	WASH_STATE_STAGE2_LOW_LEVEL_HOLD,                                 // 第二段低液位后继续保持2秒
	WASH_STATE_STAGE2_FINISH_DELAY                                    // 第二段结束后等待12秒并收尾
} WashState;                                                        // 清洗流程状态枚举

static WashState g_wash_state = WASH_STATE_IDLE;                     // 当前清洗状态
static uint32_t g_wash_state_start_ms = 0;                           // 当前状态开始时间戳

static void wash_change_state(WashState next_state, const char *message) // 切换状态并发送一次调试信息
{
	g_wash_state = next_state;                                         // 更新当前清洗状态
	log_i("%s", message);                                              // 通过 EasyLogger 输出当前进入的状态信息
}

static void wash_set_inlet(uint8_t state)                            // 设置进水电磁阀且避免重复下发命令
{
	if (FLAG_WATER_INLET_VALVE != state)                              // 仅当状态变化时才执行输出
	{
		water_inlet_solenoid_valve_Set(state);                         // 控制进水电磁阀开关
	}
}

static void wash_set_pump(uint8_t state)                             // 设置循环泵且避免重复下发命令
{
	if (FLAG_CIRCULATION_PUMP != state)                               // 仅当状态变化时才执行输出
	{
		circulation_pump_Set(state);                                   // 控制循环泵开关
	}
}

static void wash_set_circ_valve(uint8_t state)                       // 设置循环三通阀且避免重复下发命令
{
	if (FLAG_CIRCULATION_THREE_WAY != state)                          // 仅当状态变化时才执行输出
	{
		Circulation_Three_Way_Valve_Set(state);                        // 控制循环三通阀开关
	}
}

static void wash_set_outlet_valve(uint8_t state)                     // 设置出液三通阀且避免重复下发命令
{
	if (FLAG_LIQUID_OUTLET_THREE_WAY != state)                        // 仅当状态变化时才执行输出
	{
		Liquid_Outlet_Three_Way_Valve_Set(state);                      // 控制出液三通阀开关
	}
}

static void wash_set_all_closed(void)                                // 关闭清洗流程涉及的所有阀和泵
{
	wash_set_pump(0);                                                 // 关闭循环泵
	wash_set_inlet(0);                                                // 关闭进水电磁阀
	wash_set_circ_valve(0);                                           // 关闭循环三通阀
	wash_set_outlet_valve(0);                                         // 关闭出液三通阀
}

void Wash_ResetState(void)                                           // 复位整个清洗流程状态
{
	FLAG_CIRCULATION_PUMP_ENABLE = 0;                                 // 清除循环泵流程使能标志
	FLAG_LIQUID_LEVEL_SAMPLE = 0;                                     // 关闭液位采样使能标志
	wash_set_all_closed();                                            // 关闭所有相关执行器
	wash_change_state(WASH_STATE_IDLE, "WASH_STATE_IDLE");           // 回到空闲状态
	g_wash_state_start_ms = 0;                                        // 清空状态计时基准
}

void Wash_Task(void)                                                 // 每100ms调用一次的清洗状态机任务
{
	uint16_t liquid_level_adc = Liquid_Level_Read();                  // 读取当前缓存的液位ADC值

	if (FLAG_WASH_START == 0)                                         // 未收到清洗启动命令时不运行流程
	{
		if (g_wash_state != WASH_STATE_IDLE)                          // 若流程运行过则执行一次收尾复位
		{
			Wash_ResetState();                                          // 停止清洗并恢复空闲状态
		}
		return;                                                        // 直接返回，不阻塞主程序
	}

	switch (g_wash_state)                                             // 根据当前状态推进清洗流程
	{
	case WASH_STATE_IDLE:                                             // 初始空闲状态
		wash_change_state(WASH_STATE_INIT, "WASH_STATE_INIT");        // 收到启动标志后切到初始化
		break;                                                          // 本周期结束，下一周期继续

	case WASH_STATE_INIT:                                             // 流程初始化状态
		wash_set_all_closed();                                         // 先确保所有阀和泵关闭
		FLAG_CIRCULATION_PUMP_ENABLE = 0;                              // 先清除循环泵使能标志
		FLAG_LIQUID_LEVEL_SAMPLE = 1;                                  // 开启液位传感器连续采样
		g_wash_state_start_ms = delay_millis();                         // 记录第一段进水开始时间用于超时判断
		wash_change_state(WASH_STATE_STAGE1_FILL, "WASH_STATE_STAGE1_FILL"); // 进入第一段进水流程
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE1_FILL:                                      // 第一段进水状态
		wash_set_outlet_valve(0);                                      // 确保出液三通关闭
		wash_set_circ_valve(0);                                        // 确保循环三通关闭
		if (liquid_level_adc > WASH_LEVEL_PUMP_ON_THRESHOLD)           // 液位高于1000允许开循环泵
		{
			FLAG_CIRCULATION_PUMP_ENABLE = 1;                            // 置位循环泵流程使能标志
		}
		/* 30秒未达1000则超时终止 */
		if (liquid_level_adc <= WASH_LEVEL_PUMP_ON_THRESHOLD
		    && delay_expired(g_wash_state_start_ms, WASH_FILL_TIMEOUT_MS))
		{
			FLAG_WASH_START = 0;                                         // 清除启动标志，避免下一周期继续运行
			log_e("Stage1 fill timeout (30s not reaching 1000)");       // EasyLogger 输出进水超时报错
			Wash_ResetState();                                           // 终止清洗流程并复位到空闲状态
			break;                                                       // 本周期直接结束
		}
		/* 整体进水超时保护：从进水开始到达到目标2000的总时长超过120秒则终止 */
		if (delay_expired(g_wash_state_start_ms, WASH_FILL_TOTAL_TIMEOUT_MS))
		{
			FLAG_WASH_START = 0;                                         // 清除启动标志
			log_e("Water fill total timeout (120s), abort");
			Wash_ResetState();
			break;
		}
		wash_set_pump(FLAG_CIRCULATION_PUMP_ENABLE);                   // 根据使能标志控制循环泵

		if (liquid_level_adc < WASH_LEVEL_STAGE1_TARGET)               // 未达到第一段目标液位2000
		{
			wash_set_inlet(1);                                           // 保持进水阀开启继续进水
		}
		else
		{
			wash_set_inlet(0);                                           // 达到目标后关闭进水阀
			g_wash_state_start_ms = delay_millis();                      // 记录第一段浸泡开始时间
			wash_change_state(WASH_STATE_STAGE1_SOAK, "WASH_STATE_STAGE1_SOAK"); // 进入第一段60秒保持阶段
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE1_SOAK:                                      // 第一段保持状态
		wash_set_pump(FLAG_CIRCULATION_PUMP_ENABLE);                   // 保持循环泵运行状态
		if (delay_expired(g_wash_state_start_ms, WASH_SOAK_TIME_MS))   // 非阻塞判断60秒是否到时
		{
			FLAG_CIRCULATION_PUMP_ENABLE = 0;                            // 清除循环泵使能标志
			wash_set_pump(0);                                            // 先关闭循环泵
			wash_set_outlet_valve(0);                                    // 保持出液三通阀关闭
			g_wash_state_start_ms = delay_millis();                      // 记录第一段浸泡结束后的时间
			wash_change_state(WASH_STATE_STAGE1_OPEN_CIRC_DELAY, "WASH_STATE_STAGE1_OPEN_CIRC_DELAY"); // 进入开循环三通前等待
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE1_OPEN_CIRC_DELAY:                           // 第一段开循环三通前的等待状态
		if (delay_expired(g_wash_state_start_ms, WASH_SOAK_TO_CIRC_DELAY_MS)) // 非阻塞判断12秒是否到时
		{
			wash_set_circ_valve(1);                                      // 打开循环三通阀
			g_wash_state_start_ms = delay_millis();                      // 记录开循环三通后的时间
			wash_change_state(WASH_STATE_STAGE1_START_PUMP_DELAY, "WASH_STATE_STAGE1_START_PUMP_DELAY"); // 进入开泵前等待
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE1_START_PUMP_DELAY:                          // 第一段开循环三通后的等待状态
		if (delay_expired(g_wash_state_start_ms, WASH_CIRC_TO_PUMP_DELAY_MS)) // 非阻塞判断12秒是否到时
		{
			/* 排水交给 Coffee_DrainWaste_Task，清洗侧不再自己开泵 */
			FLAG_CIRCULATION_PUMP_ENABLE = 0;
			wash_set_all_closed();                                       // 清洗侧先关阀泵，排水任务会自己重新开
			FLAG_DRAIN_WASTE = 1;                                        // 触发排废液任务
			g_wash_state_start_ms = delay_millis();                      // 记录排水等待开始时间，用于兜底超时
			wash_change_state(WASH_STATE_STAGE1_LOW_LEVEL_HOLD, "WASH_STATE_STAGE1_LOW_LEVEL_HOLD (wait drain)");
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE1_LOW_LEVEL_HOLD:                            // 等待排废液完成
		/* 排废液任务完成后会清零 FLAG_DRAIN_WASTE，此处轮询等待 */
		if (FLAG_DRAIN_WASTE == 0)
		{
			g_wash_state_start_ms = delay_millis();
			wash_change_state(WASH_STATE_STAGE1_REFILL_DELAY, "WASH_STATE_STAGE1_REFILL_DELAY");
		}
		else if (delay_expired(g_wash_state_start_ms, 90000UL))         // 兜底：90s未完成则强制终止
		{
			log_e("Stage1 drain wait timeout (90s), abort wash");
			FLAG_DRAIN_WASTE = 0;                                        // 强制清零，让排水任务也复位
			FLAG_WASH_START = 0;
			Wash_ResetState();
		}
		break;

	case WASH_STATE_STAGE1_REFILL_DELAY:                              // 第一段结束后的等待状态
		if (delay_expired(g_wash_state_start_ms, WASH_REFILL_DELAY_MS)) // 非阻塞判断12秒是否到时
		{
			wash_set_inlet(1);                                           // 提前打开进水阀进入第二段进水
			g_wash_state_start_ms = delay_millis();                      // 记录第二段进水开始时间（用于总超时保护）
			wash_change_state(WASH_STATE_STAGE2_FILL, "WASH_STATE_STAGE2_FILL"); // 切换到第二段进水状态
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE2_FILL:                                      // 第二段进水状态
		if (liquid_level_adc > WASH_LEVEL_PUMP_ON_THRESHOLD)           // 液位高于1000允许开循环泵
		{
			FLAG_CIRCULATION_PUMP_ENABLE = 1;                            // 置位循环泵流程使能标志
		}
		/* 30秒未达1000则超时终止 */
		if (liquid_level_adc <= WASH_LEVEL_PUMP_ON_THRESHOLD
		    && delay_expired(g_wash_state_start_ms, WASH_FILL_TIMEOUT_MS))
		{
			FLAG_WASH_START = 0;
			log_e("Stage2 fill timeout (30s not reaching 1000)");
			Wash_ResetState();
			break;
		}
		/* 整体进水超时保护：从进入STAGE2_FILL开始超过120秒则终止 */
		if (delay_expired(g_wash_state_start_ms, WASH_FILL_TOTAL_TIMEOUT_MS))
		{
			FLAG_WASH_START = 0;
			log_e("Stage2 water fill total timeout (120s), abort");
			Wash_ResetState();
			break;
		}
		wash_set_pump(FLAG_CIRCULATION_PUMP_ENABLE);                   // 根据使能标志控制循环泵

		if (liquid_level_adc < WASH_LEVEL_STAGE2_TARGET)               // 未达到第二段目标液位3000
		{
			wash_set_inlet(1);                                           // 保持进水阀开启继续进水
		}
		else
		{
			wash_set_inlet(0);                                           // 达到目标后关闭进水阀
			g_wash_state_start_ms = delay_millis();                      // 记录第二段浸泡开始时间
			wash_change_state(WASH_STATE_STAGE2_SOAK, "WASH_STATE_STAGE2_SOAK"); // 进入第二段60秒保持阶段
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE2_SOAK:                                      // 第二段保持状态
		wash_set_pump(FLAG_CIRCULATION_PUMP_ENABLE);                   // 保持循环泵运行状态
		if (delay_expired(g_wash_state_start_ms, WASH_SOAK_TIME_MS))   // 非阻塞判断60秒是否到时
		{
			FLAG_CIRCULATION_PUMP_ENABLE = 0;                            // 清除循环泵使能标志
			wash_set_pump(0);                                            // 先关闭循环泵
			wash_set_outlet_valve(0);                                    // 保持出液三通阀关闭
			g_wash_state_start_ms = delay_millis();                      // 记录第二段浸泡结束后的时间
			wash_change_state(WASH_STATE_STAGE2_OPEN_CIRC_DELAY, "WASH_STATE_STAGE2_OPEN_CIRC_DELAY"); // 进入开循环三通前等待
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE2_OPEN_CIRC_DELAY:                           // 第二段开循环三通前的等待状态
		if (delay_expired(g_wash_state_start_ms, WASH_SOAK_TO_CIRC_DELAY_MS)) // 非阻塞判断12秒是否到时
		{
			wash_set_circ_valve(1);                                      // 打开循环三通阀
			g_wash_state_start_ms = delay_millis();                      // 记录开循环三通后的时间
			wash_change_state(WASH_STATE_STAGE2_START_PUMP_DELAY, "WASH_STATE_STAGE2_START_PUMP_DELAY"); // 进入开泵前等待
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE2_START_PUMP_DELAY:                          // 第二段开循环三通后的等待状态
		if (delay_expired(g_wash_state_start_ms, WASH_CIRC_TO_PUMP_DELAY_MS)) // 非阻塞判断12秒是否到时
		{
			FLAG_CIRCULATION_PUMP_ENABLE = 0;                            // 清洗侧不再自己开泵，交给排水任务
			wash_set_all_closed();                                       // 清洗侧先关阀泵
			FLAG_DRAIN_WASTE = 1;                                        // 触发排废液任务
			g_wash_state_start_ms = delay_millis();
			wash_change_state(WASH_STATE_STAGE2_DRAIN, "WASH_STATE_STAGE2_DRAIN (wait drain waste)");
		}
		break;

	case WASH_STATE_STAGE2_DRAIN:                                     // 第二段排液状态 → 等待排废液完成
		if (FLAG_DRAIN_WASTE == 0)
		{
			g_wash_state_start_ms = delay_millis();
			wash_change_state(WASH_STATE_STAGE2_LOW_LEVEL_HOLD, "WASH_STATE_STAGE2_LOW_LEVEL_HOLD");
		}
		else if (delay_expired(g_wash_state_start_ms, 90000UL))         // 兜底：90s未完成则强制终止
		{
			log_e("Stage2 drain wait timeout (90s), abort wash");
			FLAG_DRAIN_WASTE = 0;                                        // 强制清零，让排水任务也复位
			FLAG_WASH_START = 0;
			Wash_ResetState();
		}
		break;

	case WASH_STATE_STAGE2_LOW_LEVEL_HOLD:                            // 第二段排液完成后的等待
		if (delay_expired(g_wash_state_start_ms, WASH_REFILL_DELAY_MS)) // 等待12秒
		{
			wash_change_state(WASH_STATE_STAGE2_FINISH_DELAY, "WASH_STATE_STAGE2_FINISH_DELAY");
		}
		break;

	case WASH_STATE_STAGE2_FINISH_DELAY:                              // 第二段完成后的结束等待状态
		if (delay_expired(g_wash_state_start_ms, WASH_REFILL_DELAY_MS)) // 非阻塞判断12秒是否到时
		{
			FLAG_WASH_START = 0;                                         // 自动清除清洗启动标志
			Wash_ResetState();                                           // 执行流程复位并结束清洗
		}
		break;                                                          // 本周期结束

	default:                                                          // 异常状态保护分支
		Wash_ResetState();                                             // 出现未知状态时强制复位
		break;                                                          // 返回空闲状态
	}
}

