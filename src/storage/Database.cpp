#include "Database.h"
#include <QDir>
#include <QCoreApplication>
#include <QSqlError>
#include <QSqlQuery>

namespace sensorhub {

bool Database::open(const QString& filePath) {
    // 不传路径时用默认位置：exe 旁的 data/ 下，开发期直观可见、随手可删可备份
    // 传路径是给单元测试用的——每个用例一个临时库，互不干扰
    if (filePath.isEmpty()) {
        const QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/data");
        QDir().mkpath(dir);
        file_path_ = dir + QStringLiteral("/sensorhub.db");
    }
    else {
        file_path_ = filePath;
    }

    // 同名连接已存在时先摘掉，否则 addDatabase 会提示"重复连接名"
    // 并沿用旧连接，导致新路径被忽略
    // 测试里每个用例都新建一个库
    const QString connName = QStringLiteral("sensorhub");
    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase::removeDatabase(connName);
    }

    // 显式连接名：不依赖 Qt 的"默认连接"魔法，多连接时不会串
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    db.setDatabaseName(file_path_);
    if (!db.open()) {
        last_error_ = db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    // WAL 模式
    query.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    // 第一张表：时序读数
    query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS sensor_readings ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " sensor_id TEXT NOT NULL,"
        " type TEXT NOT NULL,"
        " value REAL NOT NULL,"
        " timestamp_ms INTEGER NOT NULL)"));

    // 时间倒序查找
    query.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_readings_sensor_ts"
        " ON sensor_readings(sensor_id, timestamp_ms DESC)"));

    // 第二张表：用户关注列表。sensor_id + type 联合唯一，防重复关注
    query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS watchlist ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " sensor_id TEXT NOT NULL,"
        " type TEXT NOT NULL,"
        " label TEXT,"
        " added_at_ms INTEGER NOT NULL,"
        " UNIQUE(sensor_id, type))"));

    // 第三张表：告警规则（用户配置）。direction 存 0/1，分别代表高于 / 低于
    query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS alarm_rules ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " sensor_id TEXT NOT NULL,"
        " type TEXT NOT NULL,"
        " threshold REAL NOT NULL,"
        " direction INTEGER NOT NULL,"
        " enabled INTEGER NOT NULL DEFAULT 1,"
        " created_at_ms INTEGER NOT NULL)"));

    // 第四张表：告警记录（程序运行时自动产生）。state 存 0/1/2 对应三种状态
    query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS alarm_events ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " rule_id INTEGER NOT NULL,"
        " sensor_id TEXT NOT NULL,"
        " type TEXT NOT NULL,"
        " value REAL NOT NULL,"
        " state INTEGER NOT NULL,"
        " triggered_at_ms INTEGER NOT NULL,"
        " resolved_at_ms INTEGER NOT NULL DEFAULT 0)"));

    // 事件列表要"没处理的排前面、按时间倒序"，建个索引让这个查询快一点
    query.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_events_state_ts"
        " ON alarm_events(state, triggered_at_ms DESC)"));

    return true;
}

QSqlDatabase Database::handle() const {
    return QSqlDatabase::database("sensorhub");
}

} // namespace sensorhub