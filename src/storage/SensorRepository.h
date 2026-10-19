#pragma once

#include "data/SensorTypes.h"
#include <QVector>

namespace sensorhub {

class Database;

// 管理 sensor_readings 表的查询
class SensorRepository {
public:
    explicit SensorRepository(Database& db) : db_(db) {}

    QVector<SensorReading> findRecent(const QString& sensorId,
                                      const QString& type, int limit);
private:
    Database& db_;
};

} // namespace sensorhub