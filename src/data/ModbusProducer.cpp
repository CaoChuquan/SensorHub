#include "ModbusProducer.h"
#include <QDateTime>
#include <QDebug>

namespace sensorhub {

namespace {

constexpr int kPollIntervalMs = 1000;   // 每秒采集一次
constexpr int kReplyTimeoutMs = 1000;   // 每超过一秒没回包就认为设备失联

constexpr quint8 kFuncReadHolding = 0x03; // 功能码 03：读保持寄存器
constexpr double kScale = 10.0;           // 寄存器存的是实际值 ×10

// 点表：第几个寄存器对应哪个机柜的哪个指标
struct Point {
    const char* sensorId;
    const char* type;
};

// 点表：第几个寄存器对应哪个机柜的哪个指标。
// 注意 sensorId 必须和 SensorScreen 里的 kRackIds 完全一致（rack1/rack2/rack3），
// 否则界面按 ID 找不到对应行，会把读数直接忽略——表现为"终端在动、界面不动"。
const Point kPoints[ModbusProducer::kRegisterCount] = {
    {"rack1", "temperature"}, {"rack1", "humidity"}, {"rack1", "voltage"},
    {"rack2", "temperature"}, {"rack2", "humidity"}, {"rack2", "voltage"},
    {"rack3", "temperature"}, {"rack3", "humidity"}, {"rack3", "voltage"},
};

// Modbus 规定多字节数字"高位在前"，Qt 没有现成的写 16 位函数，手写两行
void appendU16(QByteArray& out, quint16 value) {
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
}

quint16 readU16(const QByteArray& data, int offset) {
    return static_cast<quint16>(
        (static_cast<quint8>(data[offset]) << 8) |
         static_cast<quint8>(data[offset + 1]));
}

} // namespace

ModbusProducer::ModbusProducer(QString host, quint16 port, QObject* parent) 
    : ISensorProducer(parent), host_(host), port_(port)
{
    // 设置采集时间间隔
    pollTimer_.setInterval(kPollIntervalMs);
    // 连接 timeout 信号
    connect(&pollTimer_, &QTimer::timeout,
            this, &ModbusProducer::onPollTick);

    timeoutTimer_.setSingleShot(true);   // 设置 timeoutTimer_ 计时器只响应一次
    timeoutTimer_.setInterval(kReplyTimeoutMs);
    connect(&timeoutTimer_, &QTimer::timeout,
            this, &ModbusProducer::onRequestTimeout);
    
    // 连接 QTcpSocket 相关信号
    connect(&socket_, &QTcpSocket::connected, 
            this, &ModbusProducer::onConnected);
    connect(&socket_, &QTcpSocket::readyRead,
            this, &ModbusProducer::onReadyRead);
    connect(&socket_, &QTcpSocket::disconnected,
            this, &ModbusProducer::onDisconnected);
}

void ModbusProducer::start() {
    stopRequested_ = false;
    pollTimer_.start();
    // 发起连接
    socket_.connectToHost(host_, port_);
}

void ModbusProducer::stop() {
    stopRequested_ = true;
    pollTimer_.stop();
    timeoutTimer_.stop();
    awaitingReply_ = false;
    socket_.abort();
}

void ModbusProducer::onConnected() {
    qInfo() << "[ModbusProducer] 已连接" << host_ << port_;
    // 连接后立刻要一次数据
    sendReadRequest();    
}

void ModbusProducer::sendReadRequest() {
    QByteArray frame;
    const quint16 transactionId = nextTransactionId_++;
    appendU16(frame, transactionId); // 事务号：用来把回包和请求对上
    appendU16(frame, 0);             // 协议号，Modbus 固定 0
    appendU16(frame, 6);             // 长度：从站号 1 字节 + PDU 5 字节
    frame.append(static_cast<char>(1));                // 从站号（设备默认 1）
    frame.append(static_cast<char>(kFuncReadHolding)); // 功能码 03
    appendU16(frame, 0);              // 起始地址
    appendU16(frame, kRegisterCount); // 读几个

    awaitingReply_ = true;
    timeoutTimer_.start();
    socket_.write(frame);
}

void ModbusProducer::onPollTick() {
    // 上一帧还没回来就不发新的
    if (awaitingReply_) return;

    if (socket_.state() == QAbstractSocket::ConnectingState) return;

    if (socket_.state() != QAbstractSocket::ConnectedState) {
        // 掉线重连
        socket_.connectToHost(host_, port_);
        return;
    }

    sendReadRequest();
}

void ModbusProducer::onReadyRead() {
    rxBuffer_.append(socket_.readAll());

    // 循环解析，缓冲区里存够一帧就取一帧，不够就等下一次 readyRead
    while (rxBuffer_.size() >= 6) {
        const int frameLength = 6 + readU16(rxBuffer_, 4);
        if (rxBuffer_.size() < frameLength) break;

        const QByteArray frame = rxBuffer_.left(frameLength);
        rxBuffer_.remove(0, frameLength);
        handleFrame(frame);
    }
}

void ModbusProducer::handleFrame(const QByteArray& frame) {
    // 最短也要有：MBAP 头 7 字节 + 功能码 1 字节 + 字节数 1 字节
    if (frame.size() < 9) {
        qWarning() << "[ModbusProducer] 回包太短，丢弃";
        resetPending();
        return;
    }

    const quint16 protocolId = readU16(frame, 2);
    const quint8 functionCode = static_cast<quint8>(frame[7]);

    if (protocolId != 0) {
        qWarning() << "[ModbusProducer] 协议号不是 0，丢弃";
        resetPending();
        return;
    }

    // 0x83 = 0x03 的最高位置 1，表示设备拒绝了这次请求
    if (functionCode != kFuncReadHolding) {
        const int exceptionCode = frame.size() > 8 ? static_cast<quint8>(frame[8]) : -1;
        qWarning() << "[ModbusProducer] 设备返回错误，错误码" << exceptionCode;
        resetPending();
        return;
    }

    const int byteCount = static_cast<quint8>(frame[8]);
    if (byteCount != kRegisterCount * 2 || frame.size() < 9 + byteCount) {
        qWarning() << "[ModbusProducer] 数据长度不对，期望"
                   << kRegisterCount * 2 << "实际" << byteCount;
        resetPending();
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < kRegisterCount; ++i) {
        const quint16 raw = readU16(frame, 9 + i * 2);
        const SensorReading reading{
            QString::fromLatin1(kPoints[i].sensorId),
            QString::fromLatin1(kPoints[i].type),
            raw / kScale,
            now};
        emit newReading(reading);
    }
    resetPending();   
}

void ModbusProducer::onDisconnected() {
    rxBuffer_.clear();
    resetPending();
    if (!stopRequested_) {
        qWarning() << "[ModbusProducer] 连接断开，下一秒自动重连";
    }
}

void ModbusProducer::resetPending() {
    awaitingReply_ = false;
    timeoutTimer_.stop();
}

void ModbusProducer::onRequestTimeout() {
    qWarning() << "[ModbusProducer] 设备一秒内没回包，断开连接";
    awaitingReply_ = false;
    socket_.abort();    // 下一次轮询会自动连接
}

} // namespace sensorhub