#include "SimulationProducer.h"
#include <QDateTime>
#include <QRandomGenerator>

namespace sensorhub {

namespace {

// 被监控的 3 个 机柜
constexpr int kDeviceCount = 3;
const char* kDeviceId[kDeviceCount] = { "rack1", "rack2", "rack3" };

// 每个机柜采集 3 种指标：温度、湿度、电压
const char* const kTypes[3] = { "temperature", "humidity", "voltage" };

// 各指标基线 + 抖动幅度，贴近机房真实工况
struct Range {
    double base;
    double jitter;
};

Range rangerFor(const QString& type) {
    if (type == "temperature") return { 24.0,  2.0 };  //  24 ± 2 ℃
    if (type == "humidity")    return { 50.0, 10.0 };  //  50 ± 10 %RH
    return { 220.0, 5.0 };                             // 220 ± 5 V
}

} // namespace

SimulationProducer::SimulationProducer(QObject* parent) : QObject(parent) {
    timer_.setInterval(1000);  // 设置一秒间隔
    connect(&timer_, &QTimer::timeout, this, &SimulationProducer::produceTick);
}

void SimulationProducer::produceTick() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < kDeviceCount; ++i) {
        for (int t = 0; t < 3; ++t) {
            const QString type = QString::fromLatin1(kTypes[t]);
            const Range r = rangerFor(type);
            // generateDouble() 返回 [0,1)，映射到 [-jitter, +jitter] 再叠加基线
            const double value =
                r.base + (QRandomGenerator::global()->generateDouble() - 0.5) * 2.0 * r.jitter;
            const SensorReading reading{
                QString::fromLatin1(kDeviceId[i]), type, value, now};
            emit newReading(reading);
        }
    }    
}

} // namespace sensorhub