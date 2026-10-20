#include "services/ChartService.h"
#include "storage/SensorRepository.h"
#include "storage/WatchlistRepository.h"
#include "storage/Database.h"
#include <QSet>

namespace sensorhub {

ChartService::ChartService(Database& db, QObject* parent)
    : QObject(parent), sensorRepo_(new SensorRepository(db))
    , watchlistRepo_(new WatchlistRepository(db)) {}

ChartService::~ChartService() {
    delete sensorRepo_;
    delete watchlistRepo_;
}

QVector<ChartSeriesData> ChartService::load(const QString& type, int count) {
    QVector<ChartSeriesData> result;

    // 关注列表里筛出目标指标的条目
    const QVector<WatchlistEntry> entries = watchlistRepo_->loadAll();

    // 按 sensorId 去重：允许同一机柜被以不同 label 关注，
    // 但图上同一机柜只能画一条线，重复 ID 只保留第一次出现的条目
    QSet<QString> seen;
    QVector<WatchlistEntry> matched;
    matched.reserve(entries.size());
    for (const auto& e : entries) {
        if (e.type != type) continue;
        if (seen.contains(e.sensorId)) continue;
        seen.insert(e.sensorId);
        matched.append(e);
    }
    if (matched.isEmpty()) {
        return result;  // UI 据此提示"请先在关注列表添加该指标"
    }

    // 每个机柜一条曲线
    for (const auto& e : matched) {
        const QVector<SensorReading> readings =
            sensorRepo_->findRecent(e.sensorId, type, count);
        if (readings.isEmpty()) continue;  // 关注了但还没攒下存档，跳过

        ChartSeriesData series;
        // 图例：rack1（1 号机柜进风口）；没填标签就只显示机柜 ID
        series.name = e.label.isEmpty()
                          ? e.sensorId
                          : QStringLiteral("%1（%2）").arg(e.sensorId, e.label);

        series.points.reserve(readings.size());
        for (const auto& r : readings) {
            series.points.append(
                QPointF(static_cast<qreal>(r.timestampMs), r.value));
        }
        result.append(std::move(series));
    }
    return result;
}

} // namespace sensorhub