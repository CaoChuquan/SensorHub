#pragma once

#include "data/SensorTypes.h"
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
signals:
    // 消费者入口
    void readingReceived(const sensorhub::SensorReading& reading);
};

} // namespace sensorhub