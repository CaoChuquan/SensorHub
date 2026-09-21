#include "SensorArchiveService.h"
#include "storage/Database.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

namespace sensorhub {

SensorArchiveService::SensorArchiveService(DataHub& hub, Database& db, 
    QObject* parent) : QObject(parent), db_(db) 
{
    // 连接 DataHub，当有数据时 onReading 被激活
    connect(&hub, &DataHub::readingReceived,
            this, &SensorArchiveService::onReading);

    // 每 5 秒落盘一次
    flushtime_.setInterval(5000);
    connect(&flushtime_, &QTimer::timeout,
            this, &SensorArchiveService::flush);
    flushtime_.start();
}

void SensorArchiveService::onReading(const SensorReading& reading) {
    // 先写入 buffer，积攒一批后再落盘
    buffer_.append(reading);
}

void SensorArchiveService::flush() {
    if (buffer_.empty()) return;

    QSqlDatabase db = db_.handle();
    QSqlQuery query(db);

    // prepared statement 复用同一条 SQL 编译结果，批量插入的标准写法
    query.prepare(QStringLiteral(
        "INSERT INTO sensor_readings (sensor_id, type, value, timestamp_ms)"
        " VALUES (?, ?, ?, ?)"));

    // 核心：整批一个事务。fsync 只发生一次（commit 时）
    db.transaction();
    for (const SensorReading& r : buffer_) {
        query.addBindValue(r.sensorId);
        query.addBindValue(r.type);
        query.addBindValue(r.value);
        query.addBindValue(r.timestampMs);
        if (!query.exec()) {
            qWarning() << QStringLiteral("[archive] INSERT 失败:")
                       << query.lastError().text();
        }
    }

    if (!db.commit()) {
        qWarning() << QStringLiteral("[archive] commit 失败:")
                   << db.lastError().text();
        db.rollback(); // 整批回滚，宁可丢一批不留半批
    }
    else {
        qDebug() << QStringLiteral("[archive] 落盘") << buffer_.size()
            << QStringLiteral("条");
    }

    buffer_.clear();
}

} // namespace sensorhub