#pragma once

#include "data/SensorTypes.h"
#include "data/DeviceStatus.h"
#include <QObject>

namespace sensorhub {

// 解耦生产者和消费者，生产者将读数丢进 DataHub，消费者从 DataHub 中读取
class DataHub : public QObject {
    Q_OBJECT
public:
    explicit DataHub(QObject* parent = nullptr) : QObject(parent) {}
public slots:
    // 生产者入口
    void publish(const sensorhub::SensorReading& reading);
    // 设备在线状态变化时投递到这里
    void publishStatus(const sensorhub::DeviceStatus& status);
signals:
    // 消费者入口，当有数据时，此信号被发射
    void readingReceived(const sensorhub::SensorReading& reading);
    // 消费者出口：设备状态广播。界面据此把对应机柜的行置灰/恢复
    void deviceStatusReceived(const sensorhub::DeviceStatus& status);    
};

} // namespace sensorhub