#include "soft_i2c.h" /* 取得模拟 I2C 的起停、发送与接收接口。 */
#include "delay.h" /* 使用 SysTick 毫秒计时和短时延时接口。 */
#include "stm32f10x.h" /* 使用 STM32F1 的寄存器、外设类型和基于工程配置启用的驱动接口。 */

/*==========================================================================
 * 软件模拟 I2C 底层驱动实现
 *
 * 使用位带操作直接控制 PB8(SCL) / PB9(SDA)
 *
 * 每个显式延时为 5 μs。发送位低电平含两段延时，高电平含一段，
 * 因此频率取决于完整路径。写 1 释放开漏线路，实际高电平依赖外部上拉。
 * 此实现未读取 SCL，因而不等待从机释放时钟线。
 *==========================================================================*/

/* --- 位带宏封装 (复用 sys.h 中的 PBout/PBin) --- */
#define SCL_H()     PBout(8) = 1 /* 将 SCL_H() 展开为 「PBout(8) = 1」，供调用处复用同一操作。 */
#define SCL_L()     PBout(8) = 0 /* 将 SCL_L() 展开为 「PBout(8) = 0」，供调用处复用同一操作。 */
#define SDA_H()     PBout(9) = 1 /* 将 SDA_H() 展开为 「PBout(9) = 1」，供调用处复用同一操作。 */
#define SDA_L()     PBout(9) = 0 /* 将 SDA_L() 展开为 「PBout(9) = 0」，供调用处复用同一操作。 */
#define SDA_READ()  PBin(9) /* 将 SDA_READ() 展开为 「PBin(9)」，供调用处复用同一操作。 */

/* --- 内部宏: 释放 SDA 总线 (开漏输出写 1 即释放) --- */
#define SDA_RELEASE()   SDA_H() /* 将 SDA_RELEASE() 展开为 「SDA_H()」，供调用处复用同一操作。 */

/*==========================================================================
 * SoftI2C_Init - 初始化 PB8/PB9 为开漏输出
 *==========================================================================*/
void SoftI2C_Init(void) /* 初始化软件 I2C 的 SDA 和 SCL 引脚。 */
{
    GPIO_InitTypeDef GPIO_InitStructure; /* 声明GPIO 初始化 Structure，为后续计算或读取保存中间值。 */

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); /* 开启 「RCC_APB2Periph_GPIOB」 的外设时钟，控制相关外设时钟供给。 */

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_8 | GPIO_Pin_9; /* 把当前配置涉及的引脚掩码设为 「GPIO_Pin_8 | GPIO_Pin_9」，由随后外设初始化统一写入。 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_OD;    /* 开漏输出，配合外部上拉 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; /* 把GPIO 输出驱动的速度档位设为 「GPIO_Speed_50MHz」，由随后外设初始化统一写入。 */
    GPIO_Init(GPIOB, &GPIO_InitStructure); /* 将当前结构体中的引脚、模式和速度配置应用到 GPIOB。 */

    /* 初始状态: 总线空闲 (SCL=H, SDA=H) */
    SCL_H(); /* 执行 SCL_H 的SCL_H操作。 */
    SDA_H(); /* 执行 SDA_H 的SDA_H操作。 */
}

/*==========================================================================
 * SoftI2C_Start - 发送起始信号
 *
 * 时序: SDA 在 SCL 为高期间从高变低
 * 同一事务可再次调用该函数产生重复 START。
 *==========================================================================*/
void SoftI2C_Start(void) /* 在 SCL 为高期间拉低 SDA，形成起始或重复起始条件。 */
{
    SDA_H(); /* 执行 SDA_H 的SDA_H操作。 */
    SCL_H(); /* 执行 SCL_H 的SCL_H操作。 */
    delay_us(SOFT_I2C_DELAY_US);        /* 起始信号建立时间 ≥ 4.7μs */
    SDA_L(); /* 执行 SDA_L 的SDA_L操作。 */
    delay_us(SOFT_I2C_DELAY_US);        /* 起始信号保持时间 ≥ 4.0μs */
    SCL_L();                            /* 拉低 SCL 准备传数据 */
    delay_us(SOFT_I2C_DELAY_US); /* 按 DWT 周期忙等所需的微秒数，以提供短总线时序。 */
}

/*==========================================================================
 * SoftI2C_Stop - 发送停止信号
 *
 * 时序: SDA 在 SCL 为高期间从低变高
 * SCL 先释放，再释放 SDA，使总线回到空闲。
 *==========================================================================*/
void SoftI2C_Stop(void) /* 在 SCL 为高期间释放 SDA，结束本次总线事务。 */
{
    SCL_L(); /* 执行 SCL_L 的SCL_L操作。 */
    delay_us(SOFT_I2C_DELAY_US); /* 按 DWT 周期忙等所需的微秒数，以提供短总线时序。 */
    SDA_L(); /* 执行 SDA_L 的SDA_L操作。 */
    delay_us(SOFT_I2C_DELAY_US); /* 按 DWT 周期忙等所需的微秒数，以提供短总线时序。 */
    SCL_H(); /* 执行 SCL_H 的SCL_H操作。 */
    delay_us(SOFT_I2C_DELAY_US);        /* 停止信号建立时间 ≥ 4.0μs */
    SDA_H(); /* 执行 SDA_H 的SDA_H操作。 */
    delay_us(SOFT_I2C_DELAY_US);        /* 总线释放时间 ≥ 4.7μs */
}

/*==========================================================================
 * SoftI2C_SendByte - 发送一个字节，返回应答位
 *
 * 发送时序 (MSB first):
 *   SCL 低 → 放数据到 SDA → SCL 高(锁存) → SCL 低 → 下一 bit
 *
 * 返回: 0 = 收到 ACK,  1 = 收到 NACK
 *==========================================================================*/
uint8_t SoftI2C_SendByte(uint8_t data) /* 按高位优先发送一个 I2C 字节并读取对端的应答位。 */
{
    uint8_t i; /* 声明遍历计数器，用于扫描当前数组或记录。 */

    for (i = 0; i < 8; i++) /* 从 「i = 0」 开始，在 「i < 8」 成立时逐项处理，每轮执行 「i++」。 */
    {
        SCL_L(); /* 执行 SCL_L 的SCL_L操作。 */
        delay_us(SOFT_I2C_DELAY_US); /* 按 DWT 周期忙等所需的微秒数，以提供短总线时序。 */

        /* 放数据: MSB 先发 */
        if (data & 0x80) /* 测试待发送字节的最高位，以 I2C 要求的高位优先顺序驱动 SDA。 */
            SDA_H(); /* 执行 SDA_H 的SDA_H操作。 */
        else /* 前面的条件不满足时执行此分支。 */
            SDA_L(); /* 执行 SDA_L 的SDA_L操作。 */

        delay_us(SOFT_I2C_DELAY_US);    /* 数据建立时间 */
        SCL_H(); /* 执行 SCL_H 的SCL_H操作。 */
        delay_us(SOFT_I2C_DELAY_US);    /* SCL 高电平期间从机采样 */
        data <<= 1; /* 将数据 左移 「1」。 */
    }

    /* --- 第 9 个时钟: 读取 ACK/NACK --- */
    SCL_L(); /* 执行 SCL_L 的SCL_L操作。 */
    delay_us(SOFT_I2C_DELAY_US); /* 按 DWT 周期忙等所需的微秒数，以提供短总线时序。 */
    SDA_RELEASE();                      /* 主机释放 SDA */
    delay_us(SOFT_I2C_DELAY_US); /* 按 DWT 周期忙等所需的微秒数，以提供短总线时序。 */
    SCL_H(); /* 执行 SCL_H 的SCL_H操作。 */
    delay_us(SOFT_I2C_DELAY_US); /* 按 DWT 周期忙等所需的微秒数，以提供短总线时序。 */

    i = SDA_READ();                     /* 读 SDA: 0=ACK, 1=NACK */
    SCL_L(); /* 执行 SCL_L 的SCL_L操作。 */
    delay_us(SOFT_I2C_DELAY_US); /* 按 DWT 周期忙等所需的微秒数，以提供短总线时序。 */

    return i;                           /* 0=ACK(正常), 1=NACK(异常) */
}

/*==========================================================================
 * SoftI2C_ReadByte - 读取一个字节
 *
 * 参数 ack:  0 = 读完发送 ACK (继续读)
 *            1 = 读完发送 NACK (最后一个字节)
 *
 * 返回: 读取到的 8 位数据
 *==========================================================================*/
uint8_t SoftI2C_ReadByte(uint8_t ack) /* 从软件 I2C 读取一个字节，再依参数发送 ACK 或 NACK。 */
{
    uint8_t i, data = 0; /* 声明 「i, data = 0」，用于本函数的中间结果和循环控制。 */

    SDA_RELEASE();                      /* 主机释放 SDA，从机控制 */

    for (i = 0; i < 8; i++) /* 从 「i = 0」 开始，在 「i < 8」 成立时逐项处理，每轮执行 「i++」。 */
    {
        SCL_L(); /* 执行 SCL_L 的SCL_L操作。 */
        delay_us(SOFT_I2C_DELAY_US); /* 按 DWT 周期忙等所需的微秒数，以提供短总线时序。 */
        SCL_H(); /* 执行 SCL_H 的SCL_H操作。 */
        delay_us(SOFT_I2C_DELAY_US); /* 按 DWT 周期忙等所需的微秒数，以提供短总线时序。 */

        data <<= 1; /* 将数据 左移 「1」。 */
        if (SDA_READ()) /* 在当前时钟周期读取 SDA，把检测到的高电平记作数据位 1。 */
            data |= 0x01;               /* MSB first */ /* 将数据 按位置位 「0x01」。 */
    }

    /* --- 第 9 个时钟: 主机发送 ACK 或 NACK --- */
    SCL_L(); /* 执行 SCL_L 的SCL_L操作。 */
    delay_us(SOFT_I2C_DELAY_US); /* 按 DWT 周期忙等所需的微秒数，以提供短总线时序。 */

    if (ack) /* 应答结果表明有 NACK 时进入错误分支，结束当前 I2C 事务。 */
        SDA_H();                        /* NACK: 不需要更多数据 */
    else /* 前面的条件不满足时执行此分支。 */
        SDA_L();                        /* ACK:  继续读取 */

    delay_us(SOFT_I2C_DELAY_US); /* 按 DWT 周期忙等所需的微秒数，以提供短总线时序。 */
    SCL_H(); /* 执行 SCL_H 的SCL_H操作。 */
    delay_us(SOFT_I2C_DELAY_US); /* 按 DWT 周期忙等所需的微秒数，以提供短总线时序。 */
    SCL_L(); /* 执行 SCL_L 的SCL_L操作。 */
    delay_us(SOFT_I2C_DELAY_US); /* 按 DWT 周期忙等所需的微秒数，以提供短总线时序。 */
    SDA_RELEASE(); /* 执行 SDA_RELEASE 的SDA_RELEASE操作。 */

    return data; /* 将 「data」 返回给调用者，供上层使用。 */
}
