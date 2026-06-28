#include "stm32f10x.h"   
#include "stm32f10x_adc.h"
#include "sys.h"
#include "GPIO.h"
#include "USARTDMA.h"
#include "flag.h"

// 缓存液位传感器原始ADC值(0-4095)
static volatile uint16_t g_liquid_level_raw = 0;               // 液位传感器原始ADC值缓存
static volatile uint8_t g_liquid_level_started = 0;            // 采样状态标志(0=未开始, 1=正在采样)

// 初始化所有GPIO引脚
void GPIO_Init_ALL(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC | RCC_APB2Periph_ADC1, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    // 初始化循环泵，进水电磁阀，清洗电磁阀引脚 PA6, PA7, PA8
    GPIO_InitStructure.GPIO_Pin = circulation_pump_PIN | water_inlet_solenoid_valve_PIN | cleaning_solenoid_valve_PIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 初始化循环三通阀，出液三通阀引脚 PC4, PC6
    GPIO_InitStructure.GPIO_Pin = Circulation_Three_Way_Valve_PIN | Liquid_Outlet_Three_Way_Valve_PIN;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // 初始化液位传感器ADC引脚 PA0
    GPIO_InitStructure.GPIO_Pin = LIQUID_LEVEL_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(LIQUID_LEVEL_GPIO_PORT, &GPIO_InitStructure);

    Liquid_Level_ADC_Init();
}

// 初始化ADC1用于液位传感器采样（单次转换模式 + 软件触发）
void Liquid_Level_ADC_Init(void)
{
    ADC_InitTypeDef ADC_InitStructure;

    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;              // 改为单次模式，由软件触发控制启停
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(LIQUID_LEVEL_ADC, &ADC_InitStructure);

    ADC_Cmd(LIQUID_LEVEL_ADC, ENABLE);                       // 先使能才能校准
    ADC_ResetCalibration(LIQUID_LEVEL_ADC);
    while (ADC_GetResetCalibrationStatus(LIQUID_LEVEL_ADC) == SET)
    {
    }
    ADC_StartCalibration(LIQUID_LEVEL_ADC);
    while (ADC_GetCalibrationStatus(LIQUID_LEVEL_ADC) == SET)
    {
    }
    ADC_Cmd(LIQUID_LEVEL_ADC, DISABLE);                      // 校准后先关，由Liquid_Level_Update按需开启
}

// 采样并更新缓存值(供100ms周期调用, 非阻塞)
// 使用单次转换模式：每100ms软件触发一次ADC转换，读取EOC后更新缓存
void Liquid_Level_Update(void)
{
    if (FLAG_LIQUID_LEVEL_SAMPLE == 0)
    {
        if (g_liquid_level_started != 0)
        {
            ADC_Cmd(LIQUID_LEVEL_ADC, DISABLE);                      // 单次模式无需停转换，直接关ADC省电
            g_liquid_level_started = 0;
        }
        return;
    }

    // 首次进入采样状态：使能ADC → 配通道 → 触发首次转换
    if (g_liquid_level_started == 0)
    {
        ADC_Cmd(LIQUID_LEVEL_ADC, ENABLE);
        ADC_RegularChannelConfig(LIQUID_LEVEL_ADC, LIQUID_LEVEL_ADC_CHANNEL, 1, LIQUID_LEVEL_ADC_SAMPLE_TIME);
        ADC_SoftwareStartConvCmd(LIQUID_LEVEL_ADC, ENABLE);          // 软件触发单次转换
        g_liquid_level_started = 1;
        return;                                                       // 本次不读数据，等下一周期EOC就绪
    }

    // 单次转换完成 → 读取结果 → 立即触发下一次
    if (ADC_GetFlagStatus(LIQUID_LEVEL_ADC, ADC_FLAG_EOC) == SET)
    {
        g_liquid_level_raw = ADC_GetConversionValue(LIQUID_LEVEL_ADC);
        ADC_SoftwareStartConvCmd(LIQUID_LEVEL_ADC, ENABLE);          // 触发下一次单次转换
    }
}

// 读取缓存的原始ADC值
uint16_t Liquid_Level_Read(void)
{
    return g_liquid_level_raw;
}

// 控制循环泵PA6
void circulation_pump_Set(uint8_t state)
{
    if(state) {
        GPIO_SetBits(GPIOA, circulation_pump_PIN);
        if (FLAG_DEBUG_MODE) { USART3_DMA_SendString("CIRC_PUMP_ON\r\n"); }
        FLAG_CIRCULATION_PUMP = 1;
    } else {
        GPIO_ResetBits(GPIOA, circulation_pump_PIN);
        if (FLAG_DEBUG_MODE) { USART3_DMA_SendString("CIRC_PUMP_OFF\r\n"); }
        FLAG_CIRCULATION_PUMP = 0;
    }
}

// 控制进水电磁阀PA7
void water_inlet_solenoid_valve_Set(uint8_t state)
{
    if(state) {
        GPIO_SetBits(GPIOA, water_inlet_solenoid_valve_PIN);
        if (FLAG_DEBUG_MODE) { USART3_DMA_SendString("WATER_INLET_ON\r\n"); }
        FLAG_WATER_INLET_VALVE = 1;
    } else {
        GPIO_ResetBits(GPIOA, water_inlet_solenoid_valve_PIN);
        if (FLAG_DEBUG_MODE) { USART3_DMA_SendString("WATER_INLET_OFF\r\n"); }
        FLAG_WATER_INLET_VALVE = 0;
    }
}

// 控制清洗电磁阀PA8
void cleaning_solenoid_valve_Set(uint8_t state)
{
    if(state) {
        GPIO_SetBits(GPIOA, cleaning_solenoid_valve_PIN);
        if (FLAG_DEBUG_MODE) { USART3_DMA_SendString("CLEANING_ON\r\n"); }
        FLAG_CLEANING_VALVE = 1;
    } else {
        GPIO_ResetBits(GPIOA, cleaning_solenoid_valve_PIN);
        if (FLAG_DEBUG_MODE) { USART3_DMA_SendString("CLEANING_OFF\r\n"); }
        FLAG_CLEANING_VALVE = 0;
    }
}

// 控制循环三通阀PC4
void Circulation_Three_Way_Valve_Set(uint8_t state)
{
    if(state) {
        GPIO_SetBits(GPIOC, Circulation_Three_Way_Valve_PIN);
        if (FLAG_DEBUG_MODE) { USART3_DMA_SendString("CIRC_3WAY_ON\r\n"); }
        FLAG_CIRCULATION_THREE_WAY = 1;
    } else {
        GPIO_ResetBits(GPIOC, Circulation_Three_Way_Valve_PIN);
        if (FLAG_DEBUG_MODE) { USART3_DMA_SendString("CIRC_3WAY_OFF\r\n"); }
        FLAG_CIRCULATION_THREE_WAY = 0;
    }
}

// 控制出液三通阀PC6
void Liquid_Outlet_Three_Way_Valve_Set(uint8_t state)
{
    if(state) {
        GPIO_SetBits(GPIOC, Liquid_Outlet_Three_Way_Valve_PIN);
        if (FLAG_DEBUG_MODE) { USART3_DMA_SendString("LIQ_OUT_3WAY_ON\r\n"); }
        FLAG_LIQUID_OUTLET_THREE_WAY = 1;
    } else {
        GPIO_ResetBits(GPIOC, Liquid_Outlet_Three_Way_Valve_PIN);
        if (FLAG_DEBUG_MODE) { USART3_DMA_SendString("LIQ_OUT_3WAY_OFF\r\n"); }
        FLAG_LIQUID_OUTLET_THREE_WAY = 0;
    }
}

void set_all_gpio_low(void)  //关闭所有
{
    circulation_pump_Set(0);
    water_inlet_solenoid_valve_Set(0);
    cleaning_solenoid_valve_Set(0);
    Circulation_Three_Way_Valve_Set(0);
    Liquid_Outlet_Three_Way_Valve_Set(0);
}

