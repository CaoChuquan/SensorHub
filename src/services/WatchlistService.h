#pragma once

#include "data/SensorTypes.h"
#include <QObject>
#include <QVector>
#include <QString>

namespace sensorhub {

class WatchlistRepository;
class Database;

// 服务层：作为 UI 与存储层之间的“中介”，UI 不直接调用 WatchlistRepository
// 而是通过 WatchlistService 实现业务
class WatchlistService : public QObject {
    Q_OBJECT
public:
    explicit WatchlistService(Database& db, QObject* parent = nullptr);
    ~WatchlistService();

    // 加载全部条目
    QVector<WatchlistEntry> loadAll();
    // 添加关注
    bool add(const QString& sensorId, const QString& type,
             const QString& label);
    // 按 id 删除一条关注
    bool removeById(int id);
signals:
    // 数据变更信号：add/remove 成功后发射，UI 据此自动刷新表格
    void watchlistChanged();
private:
    WatchlistRepository* we_;
};

} // namespace sensorhub