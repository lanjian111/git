#include "sys.h"


//系统中断分组设置化		   
void NVIC_Configuration(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);	//设置NVIC中断分组4: 4位抢占优先级，0位响应优先级
}
