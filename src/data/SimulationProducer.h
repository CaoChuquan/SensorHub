#ifndef SENSORHUB_SIMULATION_PRODUCER_H
#define SENSORHUB_SIMULATION_PRODUCER_H

#include <QObject>
#include <QTimer>

#include "SensorTypes.h"

namespace sensorhub {

class SimulationProducer : public QObject {
    Q_OBJECT
public:
    explicit SimulationProducer(QObject* parent = nullptr);

    void start(); // 启动定时采样
    void stop();  // 停止采样

signals:
    // 每产生一条读数就发一次。
    void newReading(const sensorhub::SensorReading& reading);
private slots:
    void produceTick(); // 定时器槽：生成一批读数
private:
    QTimer timer_;
};

} // namespace sensorhub

#endif // SENSORHUB_SIMULATION_PRODUCER_H