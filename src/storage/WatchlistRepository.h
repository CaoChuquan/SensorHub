#pragma once

#include "data/SensorTypes.h"
#include <QVector>

namespace sensorhub {

class Database;

// 关注列表存储类，用于处理 watchlist 表的 CRUD
class WatchlistRepository {
public:
    explicit WatchlistRepository(Database& db) : db_(db) {}

    // 加载所有关注列表，按添加时间升序排列
    QVector<WatchlistEntry> loadAll();
    // 添加一条关注
    bool add(const WatchlistEntry& entry);
    // 通过 id 删除一条关注
    bool removeById(int id);
private:
    Database& db_;
};

} // namespace sensorhub