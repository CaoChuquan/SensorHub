#include "data/ISensorProducer.h"

// 接口本身没有实现的代码，这个是为了 AUTOMOC:
// 让 CMake 能扫到这个头文件里的 Q_OBJECT 和 newReading 信号，
// 自动生成信号所需的代码。删掉它会链接报错 