#pragma once

#include <QObject>
#include <QString>
#include <QPointF>
#include <Vector>

namespace sensorhub {

class Database;
class SensorRepository;
class WatchlistRepository;

// 一条曲线的数据：机柜显示名 + 若干数据点
struct ChartSeriesData {
    QString name;               // 图例名
    QVector<QPointF> points;    // 时间正序
};

class ChartService : public QObject {
    Q_OBJECT
public:
    explicit ChartService(Database& db, QObject* parent = nullptr);
    ~ChartService();

    // 加载某指标的对比曲线数据
    QVector<ChartSeriesData> load(const QString& type, int count);
private:
    SensorRepository* sensorRepo_;          // 历史读数
    WatchlistRepository* watchlistRepo_;    // 关注列表
};

} // namespace sensorhub