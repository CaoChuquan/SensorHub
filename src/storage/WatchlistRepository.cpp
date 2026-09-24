#include "WatchlistRepository.h"
#include "Database.h"
#include <QSqlQuery>
#include <QTime>

namespace sensorhub {

QVector<WatchlistEntry> WatchlistRepository::loadAll() {
    QVector<WatchlistEntry> result;
    QSqlQuery q(db_.handle());

    // 按添加时间升序：先加的排在前面
    if (!q.exec(QStringLiteral(
            "SELECT id, sensor_id, type, label, added_at_ms"
            "  FROM watchlist ORDER BY added_at_ms"))) {
        return result;  // 查询失败返回空列表
    }

    // 加载所有关注列表
    WatchlistEntry e;
    while(q.next()) {
        e.id = q.value(0).toInt();
        e.sensorId = q.value(1).toString();
        e.type = q.value(2).toString();
        e.label = q.value(3).toString();
        e.addedAtMs = q.value(4).toLongLong();

        result.append(e);
    }

    return result;
}

bool WatchlistRepository::add(const WatchlistEntry& entry) {
    QSqlQuery q(db_.handle());
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO watchlist"
        " (sensor_id, type, label, added_at_ms)"
        " VALUES (?, ?, ?, ?)"));
    q.addBindValue(entry.sensorId);
    q.addBindValue(entry.type);
    // label 为空时存 NULL
    q.addBindValue(entry.label.isEmpty() ? QVariant() : QVariant(entry.label));
    // 未设置时间戳时取当前时刻
    const qint64 ts = entry.addedAtMs > 0 ? entry.addedAtMs
                                          : QDateTime::currentMSecsSinceEpoch();
    q.addBindValue(ts);

    return q.exec();
}

bool WatchlistRepository::removeById(int id) {
    QSqlQuery q(db_.handle());
    q.prepare(QStringLiteral("DELETE FROM watchlist WHERE id = ?"));
    q.addBindValue(id);
    return q.exec();
}

} // namespace sensorhub