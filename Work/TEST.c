#include "stm32f10x.h"
#include "GPIO.h"
#include "delay.h"

/* 硬件测试模式开关：调试阶段定义 TEST_MODE 宏以启用 GPIO 逐个输出测试 */
// #define TEST_MODE

#ifdef TEST_MODE

/* 下次轮到的输出编号。每次先关五路，再打开其中一路，顺序与 switch 对应。 */
volatile uint8_t gpio_index = 0;

void TEST_Function(void)
{
    static uint32_t last_ms = 0;
    if (delay_expired(last_ms, 1000U)) // 每1秒切换一个输出
    {
        set_all_gpio_low();
        switch (gpio_index)
        {
            case 0: circulation_pump_Set(1); break;
            case 1: water_inlet_solenoid_valve_Set(1); break;
            case 2: cleaning_solenoid_valve_Set(1); break;
            case 3: Circulation_Three_Way_Valve_Set(1); break;
            case 4: Liquid_Outlet_Three_Way_Valve_Set(1); break;
        }
        gpio_index++;
        if (gpio_index > 4) gpio_index = 0;
        last_ms = delay_millis();
    }
}

#else

/* 未定义 TEST_MODE 时保留空实现，100 ms 调度入口仍按原有顺序调用。 */
void TEST_Function(void)
{
}

#endif


