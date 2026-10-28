#pragma once

#include "data/ISensorProducer.h"
#include <QTimer>

namespace sensorhub {

// 生产仿真数据
class SimulationProducer : public ISensorProducer {
    Q_OBJECT
public:
    explicit SimulationProducer(QObject* parent = nullptr);

    void start() override;
    void stop() override;
private slots:
    // 定时槽：生成一批读数
    void produceTick();
private:
    QTimer timer_;
};

} // namespace sensorhub