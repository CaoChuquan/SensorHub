#pragma once

#include "core/DataHub.h"
#include <QObject>
#include <QVector>
#include <QTimer>

namespace sensorhub {

class Database;

// 数据存档服务类：将读数落盘到 SQLite
class SensorArchiveService : public QObject {
    Q_OBJECT
public:
    SensorArchiveService(DataHub& hub, Database& db, QObject* parent = nullptr);
private slots:
    // 写入内存缓冲
    void onReading(const SensorReading& reading);
    // 批量落盘
    void flush();
private:
    Database& db_;
    QVector<SensorReading> buffer_;
    QTimer flushtime_;
};

} // namespace sensorhub