#ifndef __COFFEMAKE_H
#define __COFFEMAKE_H

/* 周期任务读取共享液位缓存，根据各自启动标志推进。排液任务结束或泵超时均清对应标志。 */
void Coffee_Task(void);                                               // 萃取流程100ms任务入口（进水+萃取）
void Coffee_DrainWaste_Task(void);                                    // 排废液流程100ms任务入口
void Coffee_DrainBrew_Task(void);                                     // 排萃取液流程100ms任务入口
void Coffee_ResetState(void);                                         // 复位萃取状态，保留屏幕设置
void Coffee_SetParam(uint16_t control_id, uint8_t *str);              // 控件 1 为升数、2 为分钟，str 须以 NUL 结尾
void Coffee_SetDrainMode(uint8_t item);                               // 原样保存选项，流程仅将 1 当作手动

#endif
