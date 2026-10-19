#include "storage/SensorRepository.h"
#include "storage/Database.h"
#include <QSqlQuery>
#include <algorithm>

namespace sensorhub {

QVector<SensorReading> SensorRepository::findRecent(const QString& sensorId,
                                                    const QString& type, int limit) {
    QVector<SensorReading> result;
    QSqlQuery q(db_.handle());

    q.prepare(QStringLiteral(
        "SELECT sensor_id, type, value, timestamp_ms"
        "  FROM sensor_readings"
        " WHERE sensor_id = ? AND type = ?"
        " ORDER BY timestamp_ms DESC"
        " LIMIT ?"));
    q.addBindValue(sensorId);
    q.addBindValue(type);
    q.addBindValue(limit);
    if (!q.exec()) {
        return result;  // 查询失败返回空列表，调用方按"没有样本"处理
    }

    while (q.next()) {
        SensorReading r;
        r.sensorId    = q.value(0).toString();
        r.type        = q.value(1).toString();
        r.value       = q.value(2).toDouble();
        r.timestampMs = q.value(3).toLongLong();
        result.append(std::move(r));
    }

    std::reverse(result.begin(), result.end());
    return result;
}

} // namespace sensorhub