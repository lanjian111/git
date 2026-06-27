#include "stm32f10x.h"
#include "delay.h"
#include "LED.h"
#include "GPIO.h"
#include "USARTDMA.h"
#include "flag.h"                                                   // 引入流程标志位定义
#include "wash.h"                                                   // 引入清洗流程接口声明

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

static void wash_debug_send(const char *message)                     // 发送清洗流程调试信息
{
	USART_DMA_SendString(message);                                     // 通过USART1发送调试字符串
}

static void wash_change_state(WashState next_state, const char *message) // 切换状态并发送一次调试信息
{
	g_wash_state = next_state;                                         // 更新当前清洗状态
	wash_debug_send(message);                                          // 输出当前进入的状态信息
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
	wash_change_state(WASH_STATE_IDLE, "WASH_STATE_IDLE\r\n");      // 回到空闲状态并发送调试信息
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
		wash_change_state(WASH_STATE_INIT, "WASH_STATE_INIT\r\n");   // 收到启动标志后切到初始化并发送调试信息
		break;                                                          // 本周期结束，下一周期继续

	case WASH_STATE_INIT:                                             // 流程初始化状态
		wash_set_all_closed();                                         // 先确保所有阀和泵关闭
		FLAG_CIRCULATION_PUMP_ENABLE = 0;                              // 先清除循环泵使能标志
		FLAG_LIQUID_LEVEL_SAMPLE = 1;                                  // 开启液位传感器连续采样
		g_wash_state_start_ms = delay_millis();                         // 记录第一段进水开始时间用于超时判断
		wash_change_state(WASH_STATE_STAGE1_FILL, "WASH_STATE_STAGE1_FILL\r\n"); // 进入第一段进水流程并发送调试信息
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE1_FILL:                                      // 第一段进水状态
		wash_set_outlet_valve(0);                                      // 确保出液三通关闭
		wash_set_circ_valve(0);                                        // 确保循环三通关闭
		if (liquid_level_adc > WASH_LEVEL_PUMP_ON_THRESHOLD)           // 液位高于1000允许开循环泵
		{
			FLAG_CIRCULATION_PUMP_ENABLE = 1;                            // 置位循环泵流程使能标志
		}
		else if (delay_expired(g_wash_state_start_ms, WASH_FILL_TIMEOUT_MS)) // 30秒未达到1000则超时终止
		{
			FLAG_WASH_START = 0;                                         // 清除启动标志，避免下一周期继续运行
			wash_debug_send("Water fill timeout during washing\r\n");    // 通过串口输出进水超时报错
			Wash_ResetState();                                           // 终止清洗流程并复位到空闲状态
			break;                                                       // 本周期直接结束
		}
		/* 整体进水超时保护：从进水开始到达到目标2000的总时长超过120秒则终止 */
		if (delay_expired(g_wash_state_start_ms, WASH_FILL_TOTAL_TIMEOUT_MS))
		{
			FLAG_WASH_START = 0;                                         // 清除启动标志
			wash_debug_send("Water fill total timeout (120s), abort\r\n");
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
			wash_change_state(WASH_STATE_STAGE1_SOAK, "WASH_STATE_STAGE1_SOAK\r\n"); // 进入第一段60秒保持阶段并发送调试信息
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
			wash_change_state(WASH_STATE_STAGE1_OPEN_CIRC_DELAY, "WASH_STATE_STAGE1_OPEN_CIRC_DELAY\r\n"); // 进入开循环三通前等待阶段并发送调试信息
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE1_OPEN_CIRC_DELAY:                           // 第一段开循环三通前的等待状态
		if (delay_expired(g_wash_state_start_ms, WASH_SOAK_TO_CIRC_DELAY_MS)) // 非阻塞判断12秒是否到时
		{
			wash_set_circ_valve(1);                                      // 打开循环三通阀
			g_wash_state_start_ms = delay_millis();                      // 记录开循环三通后的时间
			wash_change_state(WASH_STATE_STAGE1_START_PUMP_DELAY, "WASH_STATE_STAGE1_START_PUMP_DELAY\r\n"); // 进入开泵前等待阶段并发送调试信息
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE1_START_PUMP_DELAY:                          // 第一段开循环三通后的等待状态
		if (delay_expired(g_wash_state_start_ms, WASH_CIRC_TO_PUMP_DELAY_MS)) // 非阻塞判断12秒是否到时
		{
			FLAG_CIRCULATION_PUMP_ENABLE = 1;                            // 重新置位循环泵使能标志
			wash_set_pump(1);                                            // 开启循环泵进行排液循环
			wash_change_state(WASH_STATE_STAGE1_DRAIN, "WASH_STATE_STAGE1_DRAIN\r\n"); // 进入第一段排液状态并发送调试信息
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE1_DRAIN:                                     // 第一段排液状态
		wash_set_pump(FLAG_CIRCULATION_PUMP_ENABLE);                   // 保持循环泵运行排液
		if (liquid_level_adc < WASH_LEVEL_LOW_THRESHOLD)               // 液位降到100以下时进入收尾
		{
			g_wash_state_start_ms = delay_millis();                      // 记录低液位达到时刻
			wash_change_state(WASH_STATE_STAGE1_LOW_LEVEL_HOLD, "WASH_STATE_STAGE1_LOW_LEVEL_HOLD\r\n"); // 进入低液位保持2秒状态并发送调试信息
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE1_LOW_LEVEL_HOLD:                            // 第一段低液位保持状态
		if (delay_expired(g_wash_state_start_ms, WASH_LOW_LEVEL_HOLD_MS)) // 非阻塞判断2秒是否到时
		{
			FLAG_CIRCULATION_PUMP_ENABLE = 0;                            // 清除循环泵使能标志
			wash_set_pump(0);                                            // 关闭循环泵
			wash_set_circ_valve(0);                                      // 关闭循环三通阀
			wash_set_outlet_valve(0);                                    // 关闭出液三通阀
			g_wash_state_start_ms = delay_millis();                      // 记录第一段结束后的等待起点
			wash_change_state(WASH_STATE_STAGE1_REFILL_DELAY, "WASH_STATE_STAGE1_REFILL_DELAY\r\n"); // 进入第二段前等待12秒并发送调试信息
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE1_REFILL_DELAY:                              // 第一段结束后的等待状态
		if (delay_expired(g_wash_state_start_ms, WASH_REFILL_DELAY_MS)) // 非阻塞判断12秒是否到时
		{
			wash_set_inlet(1);                                           // 提前打开进水阀进入第二段进水
			g_wash_state_start_ms = delay_millis();                      // 记录第二段进水开始时间（用于总超时保护）
			wash_change_state(WASH_STATE_STAGE2_FILL, "WASH_STATE_STAGE2_FILL\r\n"); // 切换到第二段进水状态并发送调试信息
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
			wash_debug_send("Stage2 fill timeout (30s not reaching 1000)\r\n");
			Wash_ResetState();
			break;
		}
		/* 整体进水超时保护：从进入STAGE2_FILL开始超过120秒则终止 */
		if (delay_expired(g_wash_state_start_ms, WASH_FILL_TOTAL_TIMEOUT_MS))
		{
			FLAG_WASH_START = 0;
			wash_debug_send("Stage2 water fill total timeout (120s), abort\r\n");
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
			wash_change_state(WASH_STATE_STAGE2_SOAK, "WASH_STATE_STAGE2_SOAK\r\n"); // 进入第二段60秒保持阶段并发送调试信息
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
			wash_change_state(WASH_STATE_STAGE2_OPEN_CIRC_DELAY, "WASH_STATE_STAGE2_OPEN_CIRC_DELAY\r\n"); // 进入开循环三通前等待阶段并发送调试信息
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE2_OPEN_CIRC_DELAY:                           // 第二段开循环三通前的等待状态
		if (delay_expired(g_wash_state_start_ms, WASH_SOAK_TO_CIRC_DELAY_MS)) // 非阻塞判断12秒是否到时
		{
			wash_set_circ_valve(1);                                      // 打开循环三通阀
			g_wash_state_start_ms = delay_millis();                      // 记录开循环三通后的时间
			wash_change_state(WASH_STATE_STAGE2_START_PUMP_DELAY, "WASH_STATE_STAGE2_START_PUMP_DELAY\r\n"); // 进入开泵前等待阶段并发送调试信息
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE2_START_PUMP_DELAY:                          // 第二段开循环三通后的等待状态
		if (delay_expired(g_wash_state_start_ms, WASH_CIRC_TO_PUMP_DELAY_MS)) // 非阻塞判断12秒是否到时
		{
			FLAG_CIRCULATION_PUMP_ENABLE = 1;                            // 重新置位循环泵使能标志
			wash_set_pump(1);                                            // 开启循环泵进行排液循环
			wash_change_state(WASH_STATE_STAGE2_DRAIN, "WASH_STATE_STAGE2_DRAIN\r\n"); // 进入第二段排液状态并发送调试信息
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE2_DRAIN:                                     // 第二段排液状态
		wash_set_pump(FLAG_CIRCULATION_PUMP_ENABLE);                   // 保持循环泵运行排液
		if (liquid_level_adc < WASH_LEVEL_LOW_THRESHOLD)               // 液位降到100以下时进入收尾
		{
			g_wash_state_start_ms = delay_millis();                      // 记录低液位达到时刻
			wash_change_state(WASH_STATE_STAGE2_LOW_LEVEL_HOLD, "WASH_STATE_STAGE2_LOW_LEVEL_HOLD\r\n"); // 进入低液位保持2秒状态并发送调试信息
		}
		break;                                                          // 本周期结束

	case WASH_STATE_STAGE2_LOW_LEVEL_HOLD:                            // 第二段低液位保持状态
		if (delay_expired(g_wash_state_start_ms, WASH_LOW_LEVEL_HOLD_MS)) // 非阻塞判断2秒是否到时
		{
			FLAG_CIRCULATION_PUMP_ENABLE = 0;                            // 清除循环泵使能标志
			wash_set_pump(0);                                            // 关闭循环泵
			wash_set_circ_valve(0);                                      // 关闭循环三通阀
			wash_set_outlet_valve(0);                                    // 关闭出液三通阀
			g_wash_state_start_ms = delay_millis();                      // 记录第二段结束后的等待起点
			wash_change_state(WASH_STATE_STAGE2_FINISH_DELAY, "WASH_STATE_STAGE2_FINISH_DELAY\r\n"); // 进入最终结束等待状态并发送调试信息
		}
		break;                                                          // 本周期结束

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

