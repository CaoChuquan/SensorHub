#pragma once

#include "data/SensorTypes.h"
#include <QObject>
#include <QVector>

namespace sensorhub {

class Database;
class SensorRepository;

// 分析结果数据结构

// 一组样本的描述性统计量
struct AnalysisStats {
    double mean = 0.0;  // 算术均值
    double std = 0.0;   // 总体标准差（除以 N，与 numpy.std 默认口径一致）
    double min = 0.0;   // 最小值
    double max = 0.0;   // 最大值
    int count = 0;      // 实际参与计算的样本数
};

// 一个离群点：某条读数偏离均值达到若干个标准差
struct AnomalyPoint {
    qint64 timestampMs = 0;  // 读数发生时刻
    double value = 0.0;      // 读数值
    double zscore = 0.0;     // 带符号 z-score：正=偏高，负=偏低
};

class AnalysisService : public QObject {
    Q_OBJECT
public:
    explicit AnalysisService(Database& db, QObject* parent = nullptr);
    ~AnalysisService();

    // 分析指定机柜、指定指标的最近 count 条读数，结果或错误通过 analysisComplete 信号返回
    void analyzeSensor(const QString& id, const QString& type, int count = 60);
signals:
    // 分析完成后发射的信号，error 表示失败
    void analysisComplete(const QString& id, const QString& type,
                          const AnalysisStats& stats,
                          const QVector<AnomalyPoint>& anomalines,
                          const QString& error);
private:
    SensorRepository* repo_;
};

} // namespace sensorhub