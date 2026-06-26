#ifndef __GPIO_H
#define __GPIO_H

// 引脚定义
#define circulation_pump_PIN                        GPIO_Pin_6  //循环泵引脚
#define water_inlet_solenoid_valve_PIN              GPIO_Pin_7  //进水电磁阀引脚
#define cleaning_solenoid_valve_PIN                 GPIO_Pin_8  //清洗电磁阀引脚
#define Circulation_Three_Way_Valve_PIN             GPIO_Pin_4  //循环三通阀引脚
#define Liquid_Outlet_Three_Way_Valve_PIN           GPIO_Pin_6  //出液三通阀引脚

// 液位传感器(压力传感器)ADC引脚定义
#define LIQUID_LEVEL_GPIO_PORT                       GPIOA                          // 液位传感器GPIO端口
#define LIQUID_LEVEL_GPIO_PIN                        GPIO_Pin_0                     // 液位传感器GPIO引脚
#define LIQUID_LEVEL_ADC                             ADC1                           // 液位传感器ADC定义
#define LIQUID_LEVEL_ADC_CHANNEL                     ADC_Channel_0                  // 液位传感器ADC通道
#define LIQUID_LEVEL_ADC_SAMPLE_TIME                 ADC_SampleTime_239Cycles5      // 液位传感器ADC采样时间

// 初始化函数
void GPIO_Init_ALL(void);

// 控制函数
void circulation_pump_Set(uint8_t state);                   // 控制循环泵
void water_inlet_solenoid_valve_Set(uint8_t state);        // 控制进水电磁阀
void cleaning_solenoid_valve_Set(uint8_t state);           // 控制清洗电磁阀
void Circulation_Three_Way_Valve_Set(uint8_t state);      // 控制循环三通阀
void Liquid_Outlet_Three_Way_Valve_Set(uint8_t state);    // 控制出液三通阀
void set_all_gpio_low(void);                                // 设置所有GPIO引脚为低电平

// 液位传感器ADC驱动
void Liquid_Level_ADC_Init(void);                           // 初始化液位传感器ADC
void Liquid_Level_Update(void);                             // 100ms采样更新缓存
uint16_t Liquid_Level_Read(void);                           // 读取缓存的原始ADC值(0-4095)

#endif





