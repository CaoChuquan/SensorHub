#pragma once

// 用于描述机房里有哪几台设备、每台读哪些点

#include <QVector>
#include <QString>

namespace sensorhub {

// 一个采集点：把寄存器里的整数换算成有物理意义的读数
struct DevicePoint {
    QString type;              // temperature / humidity / voltage
    int registerAddress = 0;   // 从设备的第几个寄存器读，从 0 开始
    double scale = 1.0;        // 实际值 = 寄存器原始值 × scale
    QString unit;              // ℃ / %RH / V，只用于界面显示，不参与计算
};

// 一台机柜（对应一个 Modbus 从站）
struct DeviceConfig {
    QString name;              // 机柜标识，会直接作为 SensorReading::sensorId
    QString host;              // 从站所在设备的 IP（同一网关下挂多台时 IP 相同）
    quint16 port = 502;        // Modbus TCP 标准端口
    int unitId = 1;            // 从站号 1..247，同一 IP 下靠它区分不同机柜
    QVector<DevicePoint> points;
};

} // namespace sensorhub