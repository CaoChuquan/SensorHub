#ifndef SENSORHUB_SENSOR_TYPES_H
#define SENSORHUB_SENSOR_TYPES_H

#include <QMetaType>
#include <QString>

// 这里定义整个项目最底层的数据结构：一条传感器读数长什么样
// 数据源、UI、存档、告警都用它，所以放在最上层被所有模块 include
namespace sensorhub {

// 一条传感器读数
struct SensorReading {
    QString sensorId;        // 传感器标识
    QString type;            // 读数类型
    double value = 0.0;      // 数值
    qint64  timestampMs = 0; // 时间戳

    SensorReading() = default;
    SensorReading(QString id, QString t, double v, qint64 ts)
        : sensorId(std::move(id)), type(std::move(t)), value(v), timestampMs(ts) {}
};

} // namespace sensorhub

Q_DECLARE_METATYPE(sensorhub::SensorReading);

#endif // SENSORHUB_SENSOR_TYPES_H