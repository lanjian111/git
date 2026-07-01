#ifndef __COFFEMAKE_H
#define __COFFEMAKE_H

void Coffee_Task(void);                                               // 萃取流程100ms任务入口（进水+萃取）
void Coffee_DrainWaste_Task(void);                                    // 排废液流程100ms任务入口
void Coffee_DrainBrew_Task(void);                                     // 排萃取液流程100ms任务入口
void Coffee_ResetState(void);                                         // 萃取流程复位接口

#endif
