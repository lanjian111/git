#include "stm32f10x.h"
#include "GPIO.h"
#include "delay.h"

/* 硬件测试模式开关：调试阶段定义 TEST_MODE 宏以启用 GPIO 逐个输出测试 */
// #define TEST_MODE

#ifdef TEST_MODE

volatile uint8_t gpio_index = 0;

void TEST_Function(void)
{
    // static uint32_t last_ms = 0;
    // if (delay_expired(last_ms, 1000U)) // 每1秒切换一个输出
    // {
    //     set_all_gpio_low();
    //     switch (gpio_index)
    //     {
    //         case 0: circulation_pump_Set(1); break;
    //         case 1: water_inlet_solenoid_valve_Set(1); break;
    //         case 2: cleaning_solenoid_valve_Set(1); break;
    //         case 3: Circulation_Three_Way_Valve_Set(1); break;
    //         case 4: Liquid_Outlet_Three_Way_Valve_Set(1); break;
    //     }
    //     gpio_index++;
    //     if (gpio_index > 4) gpio_index = 0;
    //     last_ms = delay_millis();
    // }
}

#else

/* 正式版本：测试函数为空，编译器会内联优化掉，不产生额外开销 */
void TEST_Function(void)
{
}

#endif


