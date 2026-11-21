#pragma once

#include "data/DeviceStatus.h"
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

    // 给界面显示数据从哪里来
    virtual QString sourceDescription() const = 0;
signals:
    void newReading(const SensorReading& reading);

    // 采集链路是否正常，变了就发一次此信号
    //   online=true  ：连上了 / 正在正常出数据
    //   online=false ：断了，reason 里写原因
    void linkStateChanged(bool online, const QString& reason);
    // 单台设备的状态变化（连上/离线、离线原因）
    // 用于通知"具体哪个机柜有没有数据"，一台设备掉线只发对应一条
    void deviceStatusChanged(const sensorhub::DeviceStatus& status);    
};

} // namespace sensorhub