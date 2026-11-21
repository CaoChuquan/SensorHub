#include "ModbusProducer.h"
#include <QDateTime>
#include <QDebug>

namespace sensorhub {

namespace {

constexpr int kPollIntervalMs = 1000;      // 每秒开始一轮
constexpr int kReplyTimeoutMs = 500;       // 单台设备最多等 500 毫秒
constexpr int kMaxRegistersPerRead = 125;  // Modbus 单次读寄存器的上限
constexpr quint8 kFuncReadHolding = 0x03;  // 功能码 03：读保持寄存器
constexpr int kFailuresBeforeOffline = 3;  // 连续失败几次才判离线

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

ModbusProducer::ModbusProducer(QVector<DeviceConfig> devices, QObject* parent)
    : ISensorProducer(parent), devices_(std::move(devices)) 
{
    // 每台设备各一份状态，下标和 devices_ 对齐
    failureCounts_.fill(0, devices_.size());
    onlineFlags_.fill(false, devices_.size());
    lastSeenMs_.fill(0, devices_.size());

    if (!devices_.isEmpty()) {
        host_ = devices_.first().host;
        port_ = devices_.first().port;

        // 当前实现只支持"一台网关下挂多台从站"。
        // 如果设备表里写了不同 IP，提示当前只支持同一网关
        for (const DeviceConfig& device : devices_) {
            if (device.host != host_ || device.port != port_) {
                qWarning() << "[ModbusProducer] 设备" << device.name
                           << "配置的地址与第一台不同，当前只支持同一网关，"
                           << "将统一使用" << host_ << port_;
            }
        }
    }

    pollTimer_.setInterval(kPollIntervalMs);
    connect(&pollTimer_, &QTimer::timeout, this, &ModbusProducer::onPollTick);

    timeoutTimer_.setSingleShot(true);
    timeoutTimer_.setInterval(kReplyTimeoutMs);
    connect(&timeoutTimer_, &QTimer::timeout,
            this, &ModbusProducer::onRequestTimeout);

    connect(&socket_, &QTcpSocket::connected,
            this, &ModbusProducer::onConnected);
    connect(&socket_, &QTcpSocket::readyRead,
            this, &ModbusProducer::onReadyRead);
    connect(&socket_, &QTcpSocket::disconnected,
            this, &ModbusProducer::onDisconnected);

    // 连接失败（对方没开、地址不对、网线拔了）连接的是 errorOccurred，
    // 而不是 disconnected，因为从头到尾就没连上过，Qt 不会发 disconnected
    connect(&socket_, &QAbstractSocket::errorOccurred,
            this, [this](QAbstractSocket::SocketError) {
                if (stopRequested_) return;
                qWarning() << "[ModbusProducer] 连接失败：" << socket_.errorString();
                markAllOffline(QStringLiteral("无法连接网关"));                
                emit linkStateChanged(false,
                    QStringLiteral("连接失败，正在重连"));
            });
}

void ModbusProducer::start() {
    stopRequested_ = false;

    if (devices_.isEmpty()) {
        qWarning() << "[ModbusProducer] 设备表是空的，不会采集任何数据";
        emit linkStateChanged(false, QStringLiteral("设备表为空"));
        return;
    }

    pollTimer_.start();

    // 先告诉界面"这些设备还没采到数"，而不是假装它们在线
    // 等第一帧成功回包再逐个转成在线
    for (int i = 0; i < devices_.size(); ++i) {
        emit deviceStatusChanged({devices_.at(i).name, false, 0,
                                  QStringLiteral("等待首次采集")});
    }

    // 连接是异步的：这里只是"发起连接"，连上之后会触发 onConnected
    socket_.connectToHost(host_, port_);
}

void ModbusProducer::stop() {
    stopRequested_ = true;
    pollTimer_.stop();
    timeoutTimer_.stop();
    awaitingReply_ = false;
    pendingDeviceIndex_ = -1;
    socket_.abort();
    emit linkStateChanged(false, QStringLiteral("采集已停止"));
}

void ModbusProducer::onConnected() {
    qInfo() << "[ModbusProducer] 已连接" << host_ << port_
            << "，共" << devices_.size() << "台设备";
    emit linkStateChanged(true, QStringLiteral("已连接 %1:%2").arg(host_).arg(port_));
    beginRound(); // 不等下一秒，立刻读一轮  
}

void ModbusProducer::sendReadRequest(int deviceIndex, int firstRegister, int count) {
    const DeviceConfig& device = devices_.at(deviceIndex);

    QByteArray frame;
    const quint16 transactionId = nextTransactionId_++;
    appendU16(frame, transactionId); // 事务号：用来把回包和请求对上
    appendU16(frame, 0);             // 协议号，Modbus 固定 0
    appendU16(frame, 6);             // 长度：从站号 1 字节 + PDU 5 字节
    frame.append(static_cast<char>(device.unitId));    // 从站号：区分同一网关下的机柜
    frame.append(static_cast<char>(kFuncReadHolding)); // 功能码 03
    appendU16(frame, static_cast<quint16>(firstRegister)); // 起始地址
    appendU16(frame, static_cast<quint16>(count));         // 读几个

    pendingTransactionId_ = transactionId;
    pendingDeviceIndex_ = deviceIndex;
    pendingFirstRegister_ = firstRegister;
    pendingCount_ = count;

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

    beginRound();
}

void ModbusProducer::beginRound() {
    nextDeviceIndex_ = 0;
    sendNextRequest();
}

void ModbusProducer::sendNextRequest() {
    // 顺着设备表往下找，直到找到一台能读的；读完就结束本轮，等下一秒
    while (nextDeviceIndex_ < devices_.size()) {
        const int index = nextDeviceIndex_++;
        const DeviceConfig& device = devices_.at(index);

        if (device.points.isEmpty()) {
            qWarning() << "[ModbusProducer] 设备" << device.name << "没有采集点，跳过";
            continue;
        }

        // 点表里的寄存器可能不连续，这里读"最小到最大"的整段，
        // 再把用到的位置取出来，避免一台设备发好几条请求
        int first = std::numeric_limits<int>::max();
        int last = std::numeric_limits<int>::min();
        for (const DevicePoint& point : device.points) {
            first = std::min(first, point.registerAddress);
            last = std::max(last, point.registerAddress);
        }
        const int count = last - first + 1;

        if (count <= 0 || count > kMaxRegistersPerRead) {
            qWarning() << "[ModbusProducer] 设备" << device.name
                       << "的寄存器范围不合法（" << first << "-" << last
                       << "），跳过";
            continue;
        }

        sendReadRequest(index, first, count);
        return;
    }
    // 本轮所有设备都读完了，等下一个 tick
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
        // 不在这里刷日志：失败原因由 markFailure 在判定离线时统一说明
        markFailure(pendingDeviceIndex_, QStringLiteral("回包格式错误"));
        finishRequest();
        return;
    }

    const quint16 transactionId = readU16(frame, 0);
    const quint16 protocolId = readU16(frame, 2);

    // 超时之后迟到的回包不属于当前请求，直接丢掉，别把数据算到别的设备头上
    if (!awaitingReply_ || transactionId != pendingTransactionId_) {
        qWarning() << "[ModbusProducer] 收到过期回包（事务号"
                   << transactionId << "），丢弃";
        return;
    }

    if (protocolId != 0) {
        markFailure(pendingDeviceIndex_, QStringLiteral("回包协议号异常"));
        finishRequest();
        return;
    }

    const DeviceConfig& device = devices_.at(pendingDeviceIndex_);
    const quint8 unitId = static_cast<quint8>(frame[6]);
    const quint8 functionCode = static_cast<quint8>(frame[7]);

    if (unitId != static_cast<quint8>(device.unitId)) {
        markFailure(pendingDeviceIndex_, QStringLiteral("回包从站号不符"));
        finishRequest();
        return;
    }

    // 0x83 = 0x03 的最高位置 1，表示设备拒绝了这次请求
    if (functionCode != kFuncReadHolding) {
        const int exceptionCode = frame.size() > 8 ? static_cast<quint8>(frame[8]) : -1;
        markFailure(pendingDeviceIndex_,
                    QStringLiteral("设备返回错误码 %1").arg(exceptionCode));
        finishRequest();
        return;
    }

    const int byteCount = static_cast<quint8>(frame[8]);
    if (byteCount != pendingCount_ * 2 || frame.size() < 9 + byteCount) {
        markFailure(pendingDeviceIndex_, QStringLiteral("回包长度不符"));
        finishRequest();
        return;
    }

    markOnline(pendingDeviceIndex_);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const DevicePoint& point : device.points) {
        const int offset = 9 + (point.registerAddress - pendingFirstRegister_) * 2;
        const quint16 raw = readU16(frame, offset);
        const SensorReading reading{
            device.name,
            point.type,
            raw * point.scale,
            now};
        emit newReading(reading);
    }
    finishRequest();
}

void ModbusProducer::onDisconnected() {
    rxBuffer_.clear();
    awaitingReply_ = false;
    timeoutTimer_.stop();
    pendingDeviceIndex_ = -1;

    if (!stopRequested_) {
        qWarning() << "[ModbusProducer] 连接断开，下一秒自动重连";
        markAllOffline(QStringLiteral("网关连接断开"));
        emit linkStateChanged(false, QStringLiteral("连接断开，正在重连"));
    }
}

void ModbusProducer::onRequestTimeout() {
    // 只跳过这一台，不关连接：同一网关下的其他机柜不应该被它拖累
    markFailure(pendingDeviceIndex_,
                QStringLiteral("%1 毫秒内无响应").arg(kReplyTimeoutMs));
    finishRequest();
}

QString ModbusProducer::sourceDescription() const {
    const QString address = QStringLiteral("%1:%2").arg(host_).arg(port_);

    // Modbus 协议本身没有"我是模拟器"这种标记，程序无法真正分辨
    // 网线那头是一台真仪表还是模拟器，这里只能按地址推断：
    //   回环地址（127.0.0.1 / localhost / ::1）-> 基本可以认定是本机模拟器
    //   其他地址 -> 当作网络上的设备（可能是真表，也可能是别的机器上的模拟器）
    const bool loopback = host_ == QLatin1String("127.0.0.1")
                          || host_ == QLatin1String("localhost")
                          || host_ == QLatin1String("::1");
    return QStringLiteral("%1（%2，%3 台机柜）")
        .arg(loopback ? QStringLiteral("Modbus 本机模拟设备")
                      : QStringLiteral("Modbus 网络设备"),
             address)
        .arg(devices_.size());
}

void ModbusProducer::finishRequest() {
    awaitingReply_ = false;
    timeoutTimer_.stop();
    pendingDeviceIndex_ = -1;

    // 一台读完立刻读下一台，整轮耗时只取决于设备响应速度
    if (!stopRequested_ && socket_.state() == QAbstractSocket::ConnectedState) {
        sendNextRequest();
    }
}

void ModbusProducer::markOnline(int deviceIndex) {
    if (deviceIndex < 0 || deviceIndex >= devices_.size()) return;

    lastSeenMs_[deviceIndex] = QDateTime::currentMSecsSinceEpoch();
    failureCounts_[deviceIndex] = 0;

    // 只有"离线 → 在线"这个跳变才通知界面，正常刷新不重复发，
    // 否则每秒 3 条状态信号，界面白白重绘。
    if (!onlineFlags_[deviceIndex]) {
        onlineFlags_[deviceIndex] = true;
        qInfo() << "[ModbusProducer] 设备" << devices_.at(deviceIndex).name
                << "恢复在线";
        emit deviceStatusChanged({devices_.at(deviceIndex).name, true,
                                  lastSeenMs_[deviceIndex], QString()});
    }
}

void ModbusProducer::markFailure(int deviceIndex, const QString& reason) {
    if (deviceIndex < 0 || deviceIndex >= devices_.size()) return;

    ++failureCounts_[deviceIndex];
    if (failureCounts_[deviceIndex] != kFailuresBeforeOffline) {
        // 还没到阈值，或者已经报过离线了——都不再重复发
        return;
    }

    onlineFlags_[deviceIndex] = false;
    const QString message = QStringLiteral("%1（连续 %2 次）")
                                .arg(reason)
                                .arg(failureCounts_[deviceIndex]);
    qWarning() << "[ModbusProducer] 设备" << devices_.at(deviceIndex).name
               << "判定离线：" << message;
    emit deviceStatusChanged({devices_.at(deviceIndex).name, false,
                              lastSeenMs_[deviceIndex], message});
}

void ModbusProducer::markAllOffline(const QString& reason) {
    for (int i = 0; i < devices_.size(); ++i) {
        if (!onlineFlags_[i]) continue; // 本来就是离线的不重复通知
        onlineFlags_[i] = false;
        failureCounts_[i] = 0;
        emit deviceStatusChanged({devices_.at(i).name, false,
                                  lastSeenMs_[i], reason});
    }
}

} // namespace sensorhub