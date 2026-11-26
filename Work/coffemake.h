#ifndef __COFFEMAKE_H /* 仅在尚未定义 __COFFEMAKE_H时编译下方代码。 */
#define __COFFEMAKE_H /* 定义 __COFFEMAKE_H，供头文件防重包含或后续条件编译检查。 */

/* 周期任务读取共享液位缓存，根据各自启动标志推进。排液任务结束或泵超时均清对应标志。 */
void Coffee_Task(void);                                               // 萃取流程100ms任务入口（进水+萃取）
void Coffee_DrainWaste_Task(void);                                    // 排废液流程100ms任务入口
void Coffee_DrainBrew_Task(void);                                     // 排萃取液流程100ms任务入口
void Coffee_ResetState(void);                                         // 复位萃取状态，保留屏幕设置
void Coffee_SetParam(uint16_t control_id, uint8_t *str);              // 控件 1 为升数、2 为分钟，str 须以 NUL 结尾
void Coffee_SetDrainMode(uint8_t item);                               // 原样保存选项，流程仅将 1 当作手动

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
