#pragma once

#include "data/SensorTypes.h"
#include <QObject>

namespace sensorhub {

// 数据源接口类
// 目前从 SimulationProducer 中读取数据，之后从真实设备中读取
class ISensorProducer : public QObject {
    Q_OBJECT
public:
    explicit ISensorProducer(QObject* parent = nullptr) 
        : QObject(parent) {}
    ~ISensorProducer() override = default;

    virtual void start() = 0;
    virtual void stop() = 0;
signals:
    void newReading(const SensorReading& reading);
};

} // namespace sensorhub