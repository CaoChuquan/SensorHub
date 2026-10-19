#include "services/AnalysisService.h"
#include "storage/Database.h"
#include "storage/SensorRepository.h"
#include <algorithm>
#include <cmath>

namespace sensorhub {

namespace { constexpr double kZScoreThreshold = 3.0; }

AnalysisService::AnalysisService(Database& db, QObject* parent)
    : QObject(parent)
    , repo_(new SensorRepository(db)) {}

AnalysisService::~AnalysisService() {
    delete repo_;
}

void AnalysisService::analyzeSensor(const QString& sensorId,
                                    const QString& type, int sampleCount) {
    // 1. 取数：最近 N 条（仓储已保证时间正序）
    const QVector<SensorReading> samples =
        repo_->findRecent(sensorId, type, sampleCount);

    // 2. 空样本直接失败返回——存档服务未运行或数据库里还没有该指标数据
    if (samples.isEmpty()) {
        emit analysisComplete(sensorId, type, AnalysisStats{},
                              QVector<AnomalyPoint>{},
                              QStringLiteral("没有可分析的历史数据"));
        return;
    }

    // 3. 第一遍扫描：求和 + 最小/最大
    double sum = 0.0;
    double minV = samples.first().value;
    double maxV = samples.first().value;
    for (const auto& s : samples) {
        sum += s.value;
        minV = std::min(minV, s.value);
        maxV = std::max(maxV, s.value);
    }

    const int n = samples.size();
    const double mean = sum / n;

    // 4. 第二遍扫描：离均差平方和
    double sqSum = 0.0;
    for (const auto& s : samples) {
        const double d = s.value - mean;
        sqSum += d * d;
    }
    // 总体标准差：除以 N。样本只有 1 条时方差为 0
    const double std = std::sqrt(sqSum / n);

    AnalysisStats stats;
    stats.mean = mean;
    stats.std = std;
    stats.min = minV;
    stats.max = maxV;
    stats.count = n;

    // 5. 第三遍扫描：挑离群点。
    //    标准差为 0（所有读数完全相等）时 z-score 无定义，此时不存在异常点
    QVector<AnomalyPoint> anomalies;
    if (std > 0.0) {
        for (const auto& s : samples) {
            const double z = (s.value - mean) / std;
            if (std::fabs(z) > kZScoreThreshold) {
                anomalies.push_back({s.timestampMs, s.value, z});
            }
        }
    }

    emit analysisComplete(sensorId, type, stats, anomalies, QString());
}

} // namespace sensorhub