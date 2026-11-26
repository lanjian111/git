#include "sys.h" /* 取得中断配置和系统基础类型声明。 */


/* 在注册外设中断前统一设置 NVIC 优先级分组。 */
void NVIC_Configuration(void) /* 设置 NVIC 优先级分组，使各外设的抢占和子优先级按同一规则解释。 */
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);	//设置NVIC中断分组4: 4位抢占优先级，0位响应优先级
}
