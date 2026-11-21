#pragma once

#include <QTcpSocket>
#include <QTimer>
#include <QString>

#include "data/ISensorProducer.h"
#include "data/DeviceConfig.h"
#include "data/DeviceStatus.h"

namespace sensorhub {

/* ModbusProducer.h - 从真实设备读数据的采集器（Modbus TCP）
干什么：
  按 devices.json 一台一台地读寄存器，换算成读数发出去（newReading），
  每台设备有自己的 name、unitId、点表，所以加机柜不用改代码。

一轮轮询长什么样：
  每秒开始一轮，按设备表顺序，对每台设备发一条"读保持寄存器"请求；
  一台的回包到了就立刻读下一台，全部读完等下一秒。
  同一时刻只有一条请求在路上，回包不会对不上号。

某台设备不回包怎么办：
  只记一条告警日志，然后继续读下一台，不中断整条连接——
  否则一台表坏了会让其他机柜一起没数据。

连接断了怎么办：
  不退出程序。下一秒定时器会自己重连，界面继续留着上次的数据。*/
class ModbusProducer : public ISensorProducer {
    Q_OBJECT
public:
    explicit ModbusProducer(QVector<DeviceConfig> devices, QObject* parent = nullptr);

    void start() override;
    void stop() override;

    // 供界面显示"数据从哪来"
    QString sourceDescription() const override;
private slots:
    void onPollTick();        // 每秒触发：该发下一条请求了
    void onConnected();       // 连上设备：立刻发第一帧 
    void onReadyRead();       // 收到数据：可能是一帧、半帧或多帧
    void onDisconnected();    // 连接断开：清状态，等下次重连
    void onRequestTimeout();  // 某台设备没回包：跳过它，读下一台
private:
    void beginRound();                       // 从第一台设备开始新一轮
    void sendNextRequest();                  // 读设备表里的下一台，发完这轮就停
    void sendReadRequest(int deviceIndex, int firstRegister, int count);
    void finishRequest();                    // 当前请求结束（成功/失败都走这里）
    void handleFrame(const QByteArray& frame); // 解析一帧完整回包

    // 设备状态
    // 采集成功：刷新最后成功时间，失败计数清零；从离线恢复时报一次"在线"
    void markOnline(int deviceIndex);
    // 采集失败：累加失败次数，连续失败达到阈值才判离线，避免偶发一个丢包就在界面上闪一下红
    void markFailure(int deviceIndex, const QString& reason);
    // 整条连接断开时，把所有在线设备一次性标成离线并说明原因
    void markAllOffline(const QString& reason);

    QVector<DeviceConfig> devices_;
    QString host_;
    quint16 port_ = 502;

    QTcpSocket socket_;
    QTimer pollTimer_;     // 每秒一轮
    QTimer timeoutTimer_;  // 单条请求最多等多久
    QByteArray rxBuffer_;  // 还没凑满一帧的数据先放这里

    quint16 nextTransactionId_ = 1;
    quint16 pendingTransactionId_ = 0;
    int pendingDeviceIndex_ = -1;   // 正在等回包的是哪台设备
    int pendingFirstRegister_ = 0;  // 这条请求从哪个寄存器开始
    int pendingCount_ = 0;          // 读了多少个寄存器
    int nextDeviceIndex_ = 0;       // 本轮下一台要读的设备下标

    QVector<int> failureCounts_;   // 连续失败次数
    QVector<bool> onlineFlags_;    // 当前是否在线
    QVector<qint64> lastSeenMs_;   // 最后一次成功采集的时刻

    bool awaitingReply_ = false;
    bool stopRequested_ = false; // 是否正在主动停止，只用于在 onDisconnected 里区分
};

} // namespace sensorhub