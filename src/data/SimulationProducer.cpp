#include "SimulationProducer.h"
#include <QDateTime>
#include <QRandomGenerator>

// 仿真数据源实现
namespace sensorhub {

namespace {
// 3 个仿真设备的 ID
constexpr int kDeviceCount = 3;
const char* const kDeviceIds[kDeviceCount] = {"device01", "device02", "device03"};

// 每个设备每 tick 产 3 种读数
const char* const kTypes[3] = {"temperature", "humidity", "pressure"};

// 各类型的基线 + 抖动幅度，让仿真数据看着像真的
struct Range { double base; double jitter; };
Range rangeFor(const QString& type) {
    if (type == "temperature") return {22.0,   4.0};  // 22 ± 4 ℃
    if (type == "humidity")    return {55.0,  10.0};  // 55 ± 10 %RH
    return {1013.0, 5.0};                              // 1013 ± 5 hPa
}
} // namespace

SimulationProducer::SimulationProducer(QObject* parent)
    : QObject(parent) {
    timer_.setInterval(1000); // 每秒一帧
    connect(&timer_, &QTimer::timeout, this, &SimulationProducer::produceTick);
}

void SimulationProducer::start() { timer_.start(); }
void SimulationProducer::stop()  { timer_.stop();  }

void SimulationProducer::produceTick() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < kDeviceCount; ++i) {
        for (int t = 0; t < 3; ++t) {
            const QString type = QString::fromLatin1(kTypes[t]);
            const Range r = rangeFor(type);
            // generateDouble() 返回 [0,1)，映射到 [-jitter, +jitter] 再叠加基线
            const double value =
                r.base + (QRandomGenerator::global()->generateDouble() - 0.5) * 2.0 * r.jitter;
            const SensorReading reading{
                QString::fromLatin1(kDeviceIds[i]), type, value, now};
            emit newReading(reading);
        }
    }
}

} // namespace sensorhub
