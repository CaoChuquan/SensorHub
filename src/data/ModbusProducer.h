#pragma once

#include "data/ISensorProducer.h"
#include <QTcpSocket>
#include <QTimer>
#include <QString>

namespace sensorhub {

// 从真实设备读数据的采集器（Modbus TCP）
//
// 读哪些点（点表）：
//   从 0 号寄存器开始连续读 9 个，顺序固定：
//     0~2：rack01 的温度 / 湿度 / 电压
//     3~5：rack02 的温度 / 湿度 / 电压
//     6~8：rack03 的温度 / 湿度 / 电压
//   寄存器里存的是"实际值 ×10"的整数，比如 245 表示 24.5℃
class ModbusProducer : public ISensorProducer {
    Q_OBJECT
public:
    // 一次读取的寄存器个数，依旧是点表的长度
    static constexpr int kRegisterCount = 9;

    explicit ModbusProducer(QString host, quint16 port = 502, 
                            QObject* parent = nullptr);

    void start() override;
    void stop() override;
private slots:
    void onPollTick();        // 每秒触发：该发下一条请求了
    void onConnected();       // 连上设备：立刻发第一帧 
    void onReadyRead();       // 收到数据：可能是一帧、半帧或多帧
    void onDisconnected();    // 连接断开：清状态，等下次重连
    void onRequestTimeout();  // 一整秒没回包：断开重连
private:
    void sendReadRequest();             // 组一帧"读寄存器"请求发出去
    void handleFrame(const QByteArray& frame); // 解析一帧完整回包
    void resetPending();                // 清掉"正在等回包"的状态

    QString host_;
    quint16 port_;
    QTcpSocket socket_;
    QTimer pollTimer_;     // 采集的时间间隔
    QTimer timeoutTimer_;  // 最长等待回包的时间
    QByteArray rxBuffer_;  // 还没凑满一帧的数据先放这里
    quint16 nextTransactionId_ = 1;
    bool awaitingReply_ = false;
    // 是否正在主动停止，用于区分“用户关闭连接”和“设备掉线”
    bool stopRequested_ = false;
};

} // namespace sensorhub