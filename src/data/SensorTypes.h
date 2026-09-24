#pragma once

#include <QMetaType>
#include <QString>

// 这里定义整个项目最底层的数据结构：一条传感器读数长什么样
// 数据源、UI、存档、告警都用它，所以放在最上层被所有模块 include
namespace sensorhub {

// 机房环境读数
struct SensorReading {
    QString sensorId;        // 采集点标识
    QString type;            // 读数类型
    double value = 0.0;      // 数值
    qint64  timestampMs = 0; // 时间戳

    SensorReading() = default;
    SensorReading(QString id, QString t, double v, qint64 ts)
        : sensorId(std::move(id)), type(std::move(t)), value(v), timestampMs(ts) {}
};

// 用户在关注列表中手动添加的监控条目
// 一个 WatchlistEntry 结构变量对应
struct WatchlistEntry {
    int id = 0;             // 数据库自增主键，0 表示尚未持久化
    QString sensorId;       // 关联的采集点 ID，如 "rack01"
    QString type;           // 指标类型
    QString label;          // 用户可选自定义标签
    qint64 addedAtMs = 0;   // 添加时间，用于排序和展示

    WatchlistEntry() = default;
    WatchlistEntry(QString id, QString t, QString lbl, qint64 ts)
        : sensorId(std::move(id)), type(std::move(t))
        , label(std::move(lbl)), addedAtMs(ts) {}
};

} // namespace sensorhub

Q_DECLARE_METATYPE(sensorhub::SensorReading);