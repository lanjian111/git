#ifndef __LED_H /* 仅在尚未定义 __LED_H时编译下方代码。 */
#define __LED_H /* 定义 __LED_H，供头文件防重包含或后续条件编译检查。 */

#define LED1_PIN    GPIO_Pin_4 /* 将指示灯 1 PIN绑定到 GPIO_Pin_4，使板级配置和读写引用同一引脚定义。 */
#define LED1_PORT   GPIOA /* 将指示灯 1 PORT绑定到 GPIOA，使板级配置和读写引用同一引脚定义。 */
#define LED2_PIN    GPIO_Pin_5 /* 将指示灯 2 PIN绑定到 GPIO_Pin_5，使板级配置和读写引用同一引脚定义。 */
#define LED2_PORT   GPIOA /* 将指示灯 2 PORT绑定到 GPIOA，使板级配置和读写引用同一引脚定义。 */



/* 两路 LED 均为低电平点亮，初始化后关闭。Turn 翻转对应 GPIO 输出电平。 */
void LED_Init(void); /* 声明接口，配置状态指示灯的 GPIO，供后续开关和闪烁任务控制。 */
void LED1_ON(void); /* 声明接口，指示灯 1 ON。 */
void LED1_OFF(void); /* 声明接口，指示灯 1 OFF。 */
void LED1_Turn(void); /* 声明接口，指示灯 1 Turn。 */
void LED2_ON(void); /* 声明接口，指示灯 2 ON。 */
void LED2_OFF(void); /* 声明接口，指示灯 2 OFF。 */
void LED2_Turn(void); /* 声明接口，指示灯 2 Turn。 */
/* 由 100 ms 周期任务调用，根据 FLAG_DEBUG_MODE 选择闪烁的 LED 与间隔。 */
void LED_flicker(void); /* 声明接口，推进状态灯闪烁，用于显示周期运行和调试状态。 */

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
