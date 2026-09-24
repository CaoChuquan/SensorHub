#pragma once

#include <QString>
#include <QSqlDatabase>

namespace sensorhub {

// Database 类的作用：建目录，开 WAL，建表
class Database {
public:
    Database() = default;

    // 打开或创建数据库文件
    bool open();

    QString lastError() const { return last_error_; }
    QString filePath() const { return file_path_; }

    // 返回使用方使用连接的句柄
    QSqlDatabase handle() const;
private:
    QString last_error_;
    QString file_path_;
};

} // namespace sensorhub