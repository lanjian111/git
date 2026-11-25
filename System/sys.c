#include "sys.h"


/* 在注册外设中断前统一设置 NVIC 优先级分组。 */
void NVIC_Configuration(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);	//设置NVIC中断分组4: 4位抢占优先级，0位响应优先级
}
