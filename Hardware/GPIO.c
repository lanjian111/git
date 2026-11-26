#include "stm32f10x.h"    /* 使用 STM32F1 的寄存器、外设类型和基于工程配置启用的驱动接口。 */
#include "stm32f10x_adc.h" /* 取得 ADC 初始化、校准和结果读取所需接口。 */
#include "sys.h" /* 取得中断配置和系统基础类型声明。 */
#include "GPIO.h" /* 取得板级泵阀控制和液位采样接口。 */
#include "USARTDMA.h" /* 取得日志与触摸屏串口的 DMA 收发接口。 */
#include "flag.h" /* 取得采样、调试及流程启动标志的共享声明。 */

/* 液位传感器采用 12 位 ADC 原始值。启动时缓存为 0，停止采样时保留最后一次结果。 */
static volatile uint16_t g_liquid_level_raw = 0;               // 液位传感器原始ADC值缓存
static volatile uint8_t g_liquid_level_started = 0;            // 采样状态标志(0=未开始, 1=正在采样)

/* 初始化执行器 GPIO 和 PA0 模拟输入，随后对 ADC1 进行校准。 */
void GPIO_Init_ALL(void) /* 配置泵阀输出和液位输入，再初始化液位 ADC。 */
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC | RCC_APB2Periph_ADC1, ENABLE); /* 开启 「RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC | RCC_APB2Periph_ADC1」 的外设时钟，控制相关外设时钟供给。 */

    GPIO_InitTypeDef GPIO_InitStructure; /* 声明GPIO 初始化 Structure，为后续计算或读取保存中间值。 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; /* 把GPIO 输入或输出工作模式设为 推挽输出，由随后外设初始化统一写入。 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; /* 把GPIO 输出驱动的速度档位设为 「GPIO_Speed_50MHz」，由随后外设初始化统一写入。 */

    // 初始化循环泵，进水电磁阀，清洗电磁阀引脚 PA6, PA7, PA8
    GPIO_InitStructure.GPIO_Pin = circulation_pump_PIN | water_inlet_solenoid_valve_PIN | cleaning_solenoid_valve_PIN; /* 把当前配置涉及的引脚掩码设为 「circulation_pump_PIN | water_inlet_solenoid_valve_PIN | cleaning_solenoid_valve_PIN」，由随后外设初始化统一写入。 */
    GPIO_Init(GPIOA, &GPIO_InitStructure); /* 将当前结构体中的引脚、模式和速度配置应用到 GPIOA。 */

    // 初始化循环三通阀，出液三通阀引脚 PC4, PC6
    GPIO_InitStructure.GPIO_Pin = Circulation_Three_Way_Valve_PIN | Liquid_Outlet_Three_Way_Valve_PIN; /* 把当前配置涉及的引脚掩码设为 「Circulation_Three_Way_Valve_PIN | Liquid_Outlet_Three_Way_Valve_PIN」，由随后外设初始化统一写入。 */
    GPIO_Init(GPIOC, &GPIO_InitStructure); /* 将当前结构体中的引脚、模式和速度配置应用到 GPIOC。 */

    // 初始化液位传感器ADC引脚 PA0
    GPIO_InitStructure.GPIO_Pin = LIQUID_LEVEL_GPIO_PIN; /* 把当前配置涉及的引脚掩码设为 「LIQUID_LEVEL_GPIO_PIN」，由随后外设初始化统一写入。 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN; /* 把GPIO 输入或输出工作模式设为 模拟输入，由随后外设初始化统一写入。 */
    GPIO_Init(LIQUID_LEVEL_GPIO_PORT, &GPIO_InitStructure); /* 将当前结构体中的引脚、模式和速度配置应用到 LIQUID_LEVEL_GPIO_PORT。 */

    Liquid_Level_ADC_Init(); /* 配置单通道液位 ADC 并执行校准，供后续周期触发使用。 */
}

// 初始化ADC1用于液位传感器采样（单次转换模式 + 软件触发）
void Liquid_Level_ADC_Init(void) /* 配置单通道液位 ADC 并执行校准，供后续周期触发使用。 */
{
    ADC_InitTypeDef ADC_InitStructure; /* 声明ADC 初始化 Structure，为后续计算或读取保存中间值。 */

    RCC_ADCCLKConfig(RCC_PCLK2_Div6); /* 调用 RCC_ADCCLKConfig 处理 「RCC_PCLK2_Div6」。 */

    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent; /* 把多个 ADC 之间的工作模式设为 「ADC_Mode_Independent」，由随后外设初始化统一写入。 */
    ADC_InitStructure.ADC_ScanConvMode = DISABLE; /* 把多通道扫描开关设为 禁用（DISABLE），由随后外设初始化统一写入。 */
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;              // 改为单次模式，由软件触发控制启停
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None; /* 把规则组转换的外部触发配置设为 「ADC_ExternalTrigConv_None」，由随后外设初始化统一写入。 */
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right; /* 把转换结果在数据寄存器中的对齐方式设为 「ADC_DataAlign_Right」，由随后外设初始化统一写入。 */
    ADC_InitStructure.ADC_NbrOfChannel = 1; /* 把规则组转换序列的通道数设为 「1」，由随后外设初始化统一写入。 */
    ADC_Init(LIQUID_LEVEL_ADC, &ADC_InitStructure); /* 调用 ADC_Init 处理 「LIQUID_LEVEL_ADC, &ADC_InitStructure」。 */

    ADC_Cmd(LIQUID_LEVEL_ADC, ENABLE);                       // 先使能才能校准
    ADC_ResetCalibration(LIQUID_LEVEL_ADC); /* 复位旧校准值，准备重新校准。 */
    while (ADC_GetResetCalibrationStatus(LIQUID_LEVEL_ADC) == SET) /* 在 「ADC_GetResetCalibrationStatus(LIQUID_LEVEL_ADC) == SET」 成立期间重复处理循环体。 */
    {
    }
    ADC_StartCalibration(LIQUID_LEVEL_ADC); /* 开始 ADC 自校准，减少转换偏差。 */
    while (ADC_GetCalibrationStatus(LIQUID_LEVEL_ADC) == SET) /* 在 「ADC_GetCalibrationStatus(LIQUID_LEVEL_ADC) == SET」 成立期间重复处理循环体。 */
    {
    }
    ADC_Cmd(LIQUID_LEVEL_ADC, DISABLE);                      // 校准后先关，由Liquid_Level_Update按需开启
}

/* 100 ms 任务轮询。采样使能后的第一次调用只触发转换，后续调用看到 EOC 才更新缓存。
 * 每读完一个结果再触发下一次。若转换尚未结束，本轮保持原值并立即返回。 */
void Liquid_Level_Update(void) /* 按需启动或停止液位 ADC，完成一次转换后刷新缓存。 */
{
    if (FLAG_LIQUID_LEVEL_SAMPLE == 0) /* 当 「FLAG_LIQUID_LEVEL_SAMPLE == 0」 成立时处理下方分支。 */
    {
        if (g_liquid_level_started != 0) /* 当 「g_liquid_level_started != 0」 成立时处理下方分支。 */
        {
            ADC_Cmd(LIQUID_LEVEL_ADC, DISABLE);                      // 单次模式无需停转换，直接关ADC省电
            g_liquid_level_started = 0; /* 将液位转换启动标记 更新为 「0」。 */
        }
        return; /* 提前结束本次调用，其余处理留待后续任务或调用。 */
    }

    // 首次进入采样状态：使能ADC → 配通道 → 触发首次转换
    if (g_liquid_level_started == 0) /* 首次进入采样状态：使能ADC → 配通道 → 触发首次转换。 */
    {
        ADC_Cmd(LIQUID_LEVEL_ADC, ENABLE); /* 开启 LIQUID_LEVEL_ADC，使相应硬件进入请求的工作状态。 */
        ADC_RegularChannelConfig(LIQUID_LEVEL_ADC, LIQUID_LEVEL_ADC_CHANNEL, 1, LIQUID_LEVEL_ADC_SAMPLE_TIME); /* 设置液位传感器的 ADC 规则通道、转换次序和采样时间。 */
        ADC_SoftwareStartConvCmd(LIQUID_LEVEL_ADC, ENABLE);          // 软件触发单次转换
        g_liquid_level_started = 1; /* 将液位转换启动标记 更新为 「1」。 */
        return;                                                       // 本次不读数据，等下一周期EOC就绪
    }

    // 单次转换完成 → 读取结果 → 立即触发下一次
    if (ADC_GetFlagStatus(LIQUID_LEVEL_ADC, ADC_FLAG_EOC) == SET) /* 单次转换完成 → 读取结果 → 立即触发下一次。 */
    {
        g_liquid_level_raw = ADC_GetConversionValue(LIQUID_LEVEL_ADC); /* 将液位 ADC 结果缓存 更新为 「ADC_GetConversionValue(LIQUID_LEVEL_ADC)」。 */
        ADC_SoftwareStartConvCmd(LIQUID_LEVEL_ADC, ENABLE);          // 触发下一次单次转换
    }
}

// 读取缓存的原始ADC值
uint16_t Liquid_Level_Read(void) /* 读取最近一次已完成转换的液位原始值。 */
{
    return g_liquid_level_raw; /* 将 液位 ADC 结果缓存 返回给调用者，供上层使用。 */
}

/* 以下五个接口均使用高电平作为开启命令，state 非零即置高。
 * 每次写脚后同步更新软件标志。调试模式额外发 USART1 文本，接口本身无状态去重。
 */
// 控制循环泵PA6
void circulation_pump_Set(uint8_t state) /* 按请求控制循环泵，实际写脚后同步更新软件状态。 */
{
    if(state) { /* 当 「state」 成立时处理下方分支。 */
        GPIO_SetBits(GPIOA, circulation_pump_PIN); /* 将 「GPIOA, circulation_pump_PIN」 指定的输出置高，驱动对应引脚。 */
        if (FLAG_DEBUG_MODE) { USART_DMA_SendString("CIRC_PUMP_ON\r\n"); } /* 仅在调试模式下通过 USART1 输出当前执行器开关状态，正常模式省去调试流量。 */
        FLAG_CIRCULATION_PUMP = 1; /* 置位循环泵输出状态镜像，供后续任务判断当前状态。 */
    } else { /* 前面的条件不满足时执行此分支。 */
        GPIO_ResetBits(GPIOA, circulation_pump_PIN); /* 将 「GPIOA, circulation_pump_PIN」 指定的输出置低，驱动对应引脚。 */
        if (FLAG_DEBUG_MODE) { USART_DMA_SendString("CIRC_PUMP_OFF\r\n"); } /* 仅在调试模式下通过 USART1 输出当前执行器开关状态，正常模式省去调试流量。 */
        FLAG_CIRCULATION_PUMP = 0; /* 清零循环泵输出状态镜像，供后续任务判断当前状态。 */
    }
}

// 控制进水电磁阀PA7
void water_inlet_solenoid_valve_Set(uint8_t state) /* 按请求控制进水电磁阀，实际写脚后同步更新软件状态。 */
{
    if(state) { /* 当 「state」 成立时处理下方分支。 */
        GPIO_SetBits(GPIOA, water_inlet_solenoid_valve_PIN); /* 将 「GPIOA, water_inlet_solenoid_valve_PIN」 指定的输出置高，驱动对应引脚。 */
        if (FLAG_DEBUG_MODE) { USART_DMA_SendString("WATER_INLET_ON\r\n"); } /* 仅在调试模式下通过 USART1 输出当前执行器开关状态，正常模式省去调试流量。 */
        FLAG_WATER_INLET_VALVE = 1; /* 置位进水电磁阀状态镜像，供后续任务判断当前状态。 */
    } else { /* 前面的条件不满足时执行此分支。 */
        GPIO_ResetBits(GPIOA, water_inlet_solenoid_valve_PIN); /* 将 「GPIOA, water_inlet_solenoid_valve_PIN」 指定的输出置低，驱动对应引脚。 */
        if (FLAG_DEBUG_MODE) { USART_DMA_SendString("WATER_INLET_OFF\r\n"); } /* 仅在调试模式下通过 USART1 输出当前执行器开关状态，正常模式省去调试流量。 */
        FLAG_WATER_INLET_VALVE = 0; /* 清零进水电磁阀状态镜像，供后续任务判断当前状态。 */
    }
}

// 控制清洗电磁阀PA8
void cleaning_solenoid_valve_Set(uint8_t state) /* 按请求控制清洗电磁阀，实际写脚后同步更新软件状态。 */
{
    if(state) { /* 当 「state」 成立时处理下方分支。 */
        GPIO_SetBits(GPIOA, cleaning_solenoid_valve_PIN); /* 将 「GPIOA, cleaning_solenoid_valve_PIN」 指定的输出置高，驱动对应引脚。 */
        if (FLAG_DEBUG_MODE) { USART_DMA_SendString("CLEANING_ON\r\n"); } /* 仅在调试模式下通过 USART1 输出当前执行器开关状态，正常模式省去调试流量。 */
        FLAG_CLEANING_VALVE = 1; /* 置位清洗电磁阀状态镜像，供后续任务判断当前状态。 */
    } else { /* 前面的条件不满足时执行此分支。 */
        GPIO_ResetBits(GPIOA, cleaning_solenoid_valve_PIN); /* 将 「GPIOA, cleaning_solenoid_valve_PIN」 指定的输出置低，驱动对应引脚。 */
        if (FLAG_DEBUG_MODE) { USART_DMA_SendString("CLEANING_OFF\r\n"); } /* 仅在调试模式下通过 USART1 输出当前执行器开关状态，正常模式省去调试流量。 */
        FLAG_CLEANING_VALVE = 0; /* 清零清洗电磁阀状态镜像，供后续任务判断当前状态。 */
    }
}

// 控制循环三通阀PC4
void Circulation_Three_Way_Valve_Set(uint8_t state) /* 按请求控制循环三通阀，实际写脚后同步更新软件状态。 */
{
    if(state) { /* 当 「state」 成立时处理下方分支。 */
        GPIO_SetBits(GPIOC, Circulation_Three_Way_Valve_PIN); /* 将 「GPIOC, Circulation_Three_Way_Valve_PIN」 指定的输出置高，驱动对应引脚。 */
        if (FLAG_DEBUG_MODE) { USART_DMA_SendString("CIRC_3WAY_ON\r\n"); } /* 仅在调试模式下通过 USART1 输出当前执行器开关状态，正常模式省去调试流量。 */
        FLAG_CIRCULATION_THREE_WAY = 1; /* 置位循环三通阀状态镜像，供后续任务判断当前状态。 */
    } else { /* 前面的条件不满足时执行此分支。 */
        GPIO_ResetBits(GPIOC, Circulation_Three_Way_Valve_PIN); /* 将 「GPIOC, Circulation_Three_Way_Valve_PIN」 指定的输出置低，驱动对应引脚。 */
        if (FLAG_DEBUG_MODE) { USART_DMA_SendString("CIRC_3WAY_OFF\r\n"); } /* 仅在调试模式下通过 USART1 输出当前执行器开关状态，正常模式省去调试流量。 */
        FLAG_CIRCULATION_THREE_WAY = 0; /* 清零循环三通阀状态镜像，供后续任务判断当前状态。 */
    }
}

// 控制出液三通阀PC6
void Liquid_Outlet_Three_Way_Valve_Set(uint8_t state) /* 按请求控制出液三通阀，实际写脚后同步更新软件状态。 */
{
    if(state) { /* 当 「state」 成立时处理下方分支。 */
        GPIO_SetBits(GPIOC, Liquid_Outlet_Three_Way_Valve_PIN); /* 将 「GPIOC, Liquid_Outlet_Three_Way_Valve_PIN」 指定的输出置高，驱动对应引脚。 */
        if (FLAG_DEBUG_MODE) { USART_DMA_SendString("LIQ_OUT_3WAY_ON\r\n"); } /* 仅在调试模式下通过 USART1 输出当前执行器开关状态，正常模式省去调试流量。 */
        FLAG_LIQUID_OUTLET_THREE_WAY = 1; /* 置位出液三通阀状态镜像，供后续任务判断当前状态。 */
    } else { /* 前面的条件不满足时执行此分支。 */
        GPIO_ResetBits(GPIOC, Liquid_Outlet_Three_Way_Valve_PIN); /* 将 「GPIOC, Liquid_Outlet_Three_Way_Valve_PIN」 指定的输出置低，驱动对应引脚。 */
        if (FLAG_DEBUG_MODE) { USART_DMA_SendString("LIQ_OUT_3WAY_OFF\r\n"); } /* 仅在调试模式下通过 USART1 输出当前执行器开关状态，正常模式省去调试流量。 */
        FLAG_LIQUID_OUTLET_THREE_WAY = 0; /* 清零出液三通阀状态镜像，供后续任务判断当前状态。 */
    }
}

/* 依次关闭五路执行器输出并更新状态标志，流程启动标志由业务层管理。 */
void set_all_gpio_low(void) /* 关闭所有板级泵阀输出，并同步更新软件状态镜像。 */
{
    circulation_pump_Set(0); /* 关闭循环泵，使泵阀按当前流程阶段工作。 */
    water_inlet_solenoid_valve_Set(0); /* 关闭进水电磁阀，使泵阀按当前流程阶段工作。 */
    cleaning_solenoid_valve_Set(0); /* 关闭清洗电磁阀，使泵阀按当前流程阶段工作。 */
    Circulation_Three_Way_Valve_Set(0); /* 关闭循环三通阀，使泵阀按当前流程阶段工作。 */
    Liquid_Outlet_Three_Way_Valve_Set(0); /* 关闭出液三通阀，使泵阀按当前流程阶段工作。 */
}

