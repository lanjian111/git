#ifndef __WASH_H /* 仅在尚未定义 __WASH_H时编译下方代码。 */
#define __WASH_H /* 定义 __WASH_H，供头文件防重包含或后续条件编译检查。 */

/* 100 ms 状态机入口。FLAG_WASH_START 置位后开始两段清洗，排液用 FLAG_DRAIN_WASTE 移交。 */
void Wash_Task(void); /* 声明接口，根据当前状态推进两段清洗，每次调用执行本轮分支。 */
/* 关清洗用阀泵和采样并回空闲，调用者负责相应流程标志。 */
void Wash_ResetState(void); /* 声明接口，关闭清洗涉及的泵阀并还原清洗状态，允许下一次重新启动。 */

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
