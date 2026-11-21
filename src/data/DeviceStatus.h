#pragma once

#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace sensorhub {

// 单台设备的在线状态
struct DeviceStatus {
    QString id;             // 哪台设备（与 SensorReading::sensorId 一致）
    bool online = false;    // 这台设备最近一次采集是否成功
    qint64 lastSeenMs = 0;  // 最后一次成功拿到数据的时刻（离线时用来算"掉了多久"）
    QString error;          // 离线原因，在线时为空字符串
};

} // namespace sensorhub

// 状态会通过信号发给界面，先登记成 Qt 认识的数据类型
Q_DECLARE_METATYPE(sensorhub::DeviceStatus)