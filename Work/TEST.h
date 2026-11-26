#ifndef __TEST_H /* 仅在尚未定义 __TEST_H时编译下方代码。 */
#define __TEST_H /* 定义 __TEST_H，供头文件防重包含或后续条件编译检查。 */

/* 周期轮询。定义 TEST_MODE 时每 1 s 切换测试输出，默认编译为空函数。 */
void TEST_Function(void); /* 声明接口，执行当前编译分支下的板级测试。 */

#endif /* 结束这一组条件编译，后面的代码恢复外层编译条件。 */
