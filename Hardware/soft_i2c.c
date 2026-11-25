#include "soft_i2c.h"
#include "delay.h"
#include "stm32f10x.h"

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
#define SCL_H()     PBout(8) = 1
#define SCL_L()     PBout(8) = 0
#define SDA_H()     PBout(9) = 1
#define SDA_L()     PBout(9) = 0
#define SDA_READ()  PBin(9)

/* --- 内部宏: 释放 SDA 总线 (开漏输出写 1 即释放) --- */
#define SDA_RELEASE()   SDA_H()

/*==========================================================================
 * SoftI2C_Init - 初始化 PB8/PB9 为开漏输出
 *==========================================================================*/
void SoftI2C_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_OD;    /* 开漏输出，配合外部上拉 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* 初始状态: 总线空闲 (SCL=H, SDA=H) */
    SCL_H();
    SDA_H();
}

/*==========================================================================
 * SoftI2C_Start - 发送起始信号
 *
 * 时序: SDA 在 SCL 为高期间从高变低
 * 同一事务可再次调用该函数产生重复 START。
 *==========================================================================*/
void SoftI2C_Start(void)
{
    SDA_H();
    SCL_H();
    delay_us(SOFT_I2C_DELAY_US);        /* 起始信号建立时间 ≥ 4.7μs */
    SDA_L();
    delay_us(SOFT_I2C_DELAY_US);        /* 起始信号保持时间 ≥ 4.0μs */
    SCL_L();                            /* 拉低 SCL 准备传数据 */
    delay_us(SOFT_I2C_DELAY_US);
}

/*==========================================================================
 * SoftI2C_Stop - 发送停止信号
 *
 * 时序: SDA 在 SCL 为高期间从低变高
 * SCL 先释放，再释放 SDA，使总线回到空闲。
 *==========================================================================*/
void SoftI2C_Stop(void)
{
    SCL_L();
    delay_us(SOFT_I2C_DELAY_US);
    SDA_L();
    delay_us(SOFT_I2C_DELAY_US);
    SCL_H();
    delay_us(SOFT_I2C_DELAY_US);        /* 停止信号建立时间 ≥ 4.0μs */
    SDA_H();
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
uint8_t SoftI2C_SendByte(uint8_t data)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        SCL_L();
        delay_us(SOFT_I2C_DELAY_US);

        /* 放数据: MSB 先发 */
        if (data & 0x80)
            SDA_H();
        else
            SDA_L();

        delay_us(SOFT_I2C_DELAY_US);    /* 数据建立时间 */
        SCL_H();
        delay_us(SOFT_I2C_DELAY_US);    /* SCL 高电平期间从机采样 */
        data <<= 1;
    }

    /* --- 第 9 个时钟: 读取 ACK/NACK --- */
    SCL_L();
    delay_us(SOFT_I2C_DELAY_US);
    SDA_RELEASE();                      /* 主机释放 SDA */
    delay_us(SOFT_I2C_DELAY_US);
    SCL_H();
    delay_us(SOFT_I2C_DELAY_US);

    i = SDA_READ();                     /* 读 SDA: 0=ACK, 1=NACK */
    SCL_L();
    delay_us(SOFT_I2C_DELAY_US);

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
uint8_t SoftI2C_ReadByte(uint8_t ack)
{
    uint8_t i, data = 0;

    SDA_RELEASE();                      /* 主机释放 SDA，从机控制 */

    for (i = 0; i < 8; i++)
    {
        SCL_L();
        delay_us(SOFT_I2C_DELAY_US);
        SCL_H();
        delay_us(SOFT_I2C_DELAY_US);

        data <<= 1;
        if (SDA_READ())
            data |= 0x01;               /* MSB first */
    }

    /* --- 第 9 个时钟: 主机发送 ACK 或 NACK --- */
    SCL_L();
    delay_us(SOFT_I2C_DELAY_US);

    if (ack)
        SDA_H();                        /* NACK: 不需要更多数据 */
    else
        SDA_L();                        /* ACK:  继续读取 */

    delay_us(SOFT_I2C_DELAY_US);
    SCL_H();
    delay_us(SOFT_I2C_DELAY_US);
    SCL_L();
    delay_us(SOFT_I2C_DELAY_US);
    SDA_RELEASE();

    return data;
}
