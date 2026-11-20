#pragma once

#include "DeviceConfig.h"

namespace sensorhub {

// 用于读取 device.json 并效验
class DeviceConfigLoader {
public:
    // 从 JSON 文件加载设备表
    // 返回空列表且 error 非空表示失败
    // error 允许传 nullptr，表示调用方不关心具体原因
    static QVector<DeviceConfig> loadFromFile(const QString& path, QString* error);
    // 内置默认配置：3 台机柜，接本机 127.0.0.1:502，从站号 1/2/3，
    // 每台读温度/湿度/电压三个寄存器（原始值 ×0.1）
    static QVector<DeviceConfig> defaults();
};

} // namespace sensorhub