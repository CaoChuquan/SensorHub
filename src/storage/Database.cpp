#include "Database.h"
#include <QDir>
#include <QCoreApplication>
#include <QSqlError>
#include <QSqlQuery>

namespace sensorhub {

bool Database::open() {
    // 数据库文件暂时放在与可执行文件同目录下的 temp_database 目录下
    const QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/temp_database");
    QDir().mkpath(dir);  // 创建目录
    file_path_ = dir + QStringLiteral("/sensorhub.db");

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("sensorhub"));
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

    return true;
}

QSqlDatabase Database::handle() const {
    return QSqlDatabase::database("sensorhub");
}

} // namespace sensorhub