#pragma once

#include "SensorTypes.h"
#include <QObject>
#include <QTimer>

namespace sensorhub {

// 生产仿真数据
class SimulationProducer : public QObject {
    Q_OBJECT
public:
    explicit SimulationProducer(QObject* parent = nullptr);

    // 启动定时采样
    void start() { timer_.start(); } 
    // 停止采样
    void stop() { timer_.stop(); }    
signals:
    // 发送读数
    void newReading(const sensorhub::SensorReading& reading);
private slots:
    // 定时槽：生成一批读数
    void produceTick();
private:
    QTimer timer_;
};

} // namespace sensorhub