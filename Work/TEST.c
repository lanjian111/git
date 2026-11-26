#include "stm32f10x.h" /* 使用 STM32F1 的寄存器、外设类型和基于工程配置启用的驱动接口。 */
#include "GPIO.h" /* 取得板级泵阀控制和液位采样接口。 */
#include "delay.h" /* 使用 SysTick 毫秒计时和短时延时接口。 */

/* 硬件测试模式开关：调试阶段定义 TEST_MODE 宏以启用 GPIO 逐个输出测试 */
// #define TEST_MODE

#ifdef TEST_MODE /* 仅在已定义 TEST_MODE时编译下方代码。 */

/* 下次轮到的输出编号。每次先关五路，再打开其中一路，顺序与 switch 对应。 */
volatile uint8_t gpio_index = 0; /* 将GPIO 下标初始化为零，给模块一个确定的起始值。 */

void TEST_Function(void) /* 执行当前编译分支下的板级测试。 */
{
    static uint32_t last_ms = 0; /* 将last 毫秒初始化为零，给模块一个确定的起始值。 */
    if (delay_expired(last_ms, 1000U)) // 每1秒切换一个输出
    {
        set_all_gpio_low(); /* 关闭所有板级泵阀输出，并同步更新软件状态镜像。 */
        switch (gpio_index) /* 按GPIO 下标的取值选择对应的处理分支。 */
        {
            case 0: circulation_pump_Set(1); break; /* 进入 0 对应的消息或取值处理分支。 */
            case 1: water_inlet_solenoid_valve_Set(1); break; /* 进入 1 对应的消息或取值处理分支。 */
            case 2: cleaning_solenoid_valve_Set(1); break; /* 进入 2 对应的消息或取值处理分支。 */
            case 3: Circulation_Three_Way_Valve_Set(1); break; /* 进入 3 对应的消息或取值处理分支。 */
            case 4: Liquid_Outlet_Three_Way_Valve_Set(1); break; /* 进入 4 对应的消息或取值处理分支。 */
        }
        gpio_index++; /* 将GPIO 下标加一，推进处理进度。 */
        if (gpio_index > 4) gpio_index = 0; /* 当 「gpio_index > 4」 成立时，将GPIO 下标 更新为 「0」。 */
        last_ms = delay_millis(); /* 把当前毫秒节拍保存到 last_ms，作为之后计算时间差的基准。 */
    }
}

#else /* 前面的编译条件不成立时，改用下方实现或配置。 */

/* 未定义 TEST_MODE 时保留空实现，100 ms 调度入口仍按原有顺序调用。 */
void TEST_Function(void) /* 执行当前编译分支下的板级测试。 */
{
}

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */


