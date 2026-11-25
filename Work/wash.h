#ifndef __WASH_H
#define __WASH_H

/* 100 ms 状态机入口。FLAG_WASH_START 置位后开始两段清洗，排液用 FLAG_DRAIN_WASTE 移交。 */
void Wash_Task(void);
/* 关清洗用阀泵和采样并回空闲，调用者负责相应流程标志。 */
void Wash_ResetState(void);

#endif
