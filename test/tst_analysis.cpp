// 统计分析（均值/标准差/3σ 离群点）的单元测试
//
// 为什么值得测：
//   统计结果"看起来差不多"最容易骗人 —— 标准差算成了样本标准差、
//   或者一条数据都没有时除零，界面上都不会报错，只会给出一个错误的数字
//   这些只能靠固定输入的定值断言发现
//
// 测试方式：
//   直接往 sensor_readings 表插数据（模拟历史存档已经有了），
//   然后调 analyzeSensor，从 analysisComplete 信号里取结果
#include <cmath>

#include <QSqlError>
#include <QSqlQuery>
#include <QTest>

#include "services/AnalysisService.h"
#include "storage/Database.h"

using namespace sensorhub;

class TstAnalysis : public QObject {
    Q_OBJECT
private slots:
    void init();     // 每个用例：临时库 + 服务实例
    void cleanup();

    // 没有样本时应返回错误，而不是给出一堆 0
    void emptySamplesReturnsError();
    // 只有一个样本：均值就是它本身，标准差 0，不能报异常
    void singleSampleHasZeroStd();
    // {10, 20, 30}：均值 20，总体标准差 sqrt(200/3)，极值 10/30
    void computesMeanAndStdOfKnownSeries();
    // 正好卡在 |z| == 3 的点不算离群点（判定用的是严格大于）
    void pointExactlyAtThreeSigmaIsNotAnomaly();
    // 只取最新 N 条：样本数等于上限，且旧数据不参与计算
    void respectsSampleCountLimit();
    // 所有值相等：标准差为 0，不能因为除零而产生离群点
    void allEqualValuesHaveZeroStdAndNoAnomaly();
    // 12 个正常值 + 1 个 200：应识别出那一个离群点
    void detectsOutlierBeyondThreeSigma();
private:
    // 直接插一条历史读数（绕过采集链路，只关心分析结果）
    void insertReading(const QString& sensorId, const QString& type,
                       double value, qint64 timestampMs);

    Database* db_ = nullptr;
    AnalysisService* service_ = nullptr;

    // 信号回调写进这些成员，用例里直接断言
    AnalysisStats stats_;
    QVector<AnomalyPoint> anomalies_;
    QString error_;
    bool completed_ = false;
};

void TstAnalysis::init() {
    completed_ = false;
    stats_ = AnalysisStats{};
    anomalies_.clear();
    error_.clear();

    // 内存库：每次 open 前 Database 会移除同名旧连接，所以每个用例都是全新的空库，
    // 不落盘、不留下残留文件，跑完即消失。
    db_ = new Database;
    QVERIFY2(db_->open(QStringLiteral(":memory:")), qPrintable(db_->lastError()));

    service_ = new AnalysisService(*db_);

    // 服务是"同步计算 + 发信号"，所以调用返回后这些成员就已经填好了
    connect(service_, &AnalysisService::analysisComplete, this,
            [this](const QString&, const QString&,
                   const AnalysisStats& stats,
                   const QVector<AnomalyPoint>& anomalies,
                   const QString& error) {
                stats_ = stats;
                anomalies_ = anomalies;
                error_ = error;
                completed_ = true;
            });
}

void TstAnalysis::cleanup() {
    delete service_;   // 服务内部持有仓储，必须先于数据库销毁
    service_ = nullptr;
    delete db_;
    db_ = nullptr;
}

void TstAnalysis::insertReading(const QString& sensorId, const QString& type,
                                double value, qint64 timestampMs) {
    QSqlQuery q(db_->handle());
    q.prepare(QStringLiteral(
        "INSERT INTO sensor_readings (sensor_id, type, value, timestamp_ms)"
        " VALUES (?, ?, ?, ?)"));
    q.addBindValue(sensorId);
    q.addBindValue(type);
    q.addBindValue(value);
    q.addBindValue(timestampMs);
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
}

void TstAnalysis::emptySamplesReturnsError() {
    service_->analyzeSensor(QStringLiteral("rack1"), QStringLiteral("temperature"), 60);

    QVERIFY(completed_);
    QVERIFY(!error_.isEmpty());     // 有错误提示
    QCOMPARE(stats_.count, 0);
    QVERIFY(anomalies_.isEmpty());
}

void TstAnalysis::singleSampleHasZeroStd() {
    insertReading(QStringLiteral("rack1"), QStringLiteral("temperature"), 42.0, 1000);

    service_->analyzeSensor(QStringLiteral("rack1"), QStringLiteral("temperature"), 60);

    QVERIFY(completed_);
    QVERIFY(error_.isEmpty());
    QCOMPARE(stats_.count, 1);
    QVERIFY(qAbs(stats_.mean - 42.0) < 1e-9);
    QVERIFY(qAbs(stats_.std) < 1e-12);
    QVERIFY(anomalies_.isEmpty());
}

void TstAnalysis::computesMeanAndStdOfKnownSeries() {
    insertReading(QStringLiteral("rack1"), QStringLiteral("temperature"), 10.0, 1000);
    insertReading(QStringLiteral("rack1"), QStringLiteral("temperature"), 20.0, 2000);
    insertReading(QStringLiteral("rack1"), QStringLiteral("temperature"), 30.0, 3000);

    service_->analyzeSensor(QStringLiteral("rack1"), QStringLiteral("temperature"), 60);

    QVERIFY(completed_);
    QVERIFY(error_.isEmpty());
    QCOMPARE(stats_.count, 3);
    QVERIFY(qAbs(stats_.mean - 20.0) < 1e-9);
    // 总体标准差：sqrt(((10-20)^2 + 0 + (30-20)^2) / 3) = sqrt(200/3)
    QVERIFY(qAbs(stats_.std - 8.16496580927726) < 1e-6);
    QVERIFY(qAbs(stats_.min - 10.0) < 1e-9);
    QVERIFY(qAbs(stats_.max - 30.0) < 1e-9);
}

void TstAnalysis::allEqualValuesHaveZeroStdAndNoAnomaly() {
    for (int i = 0; i < 5; ++i) {
        insertReading(QStringLiteral("rack1"), QStringLiteral("humidity"),
                      50.0, 1000 + i * 1000);
    }

    service_->analyzeSensor(QStringLiteral("rack1"), QStringLiteral("humidity"), 60);

    QVERIFY(completed_);
    QVERIFY(error_.isEmpty());
    QCOMPARE(stats_.count, 5);
    QVERIFY(qAbs(stats_.std) < 1e-12);   // 标准差为 0
    QVERIFY(anomalies_.isEmpty());       // 且不能因为除以 0 而报异常
}

void TstAnalysis::pointExactlyAtThreeSigmaIsNotAnomaly() {
    // 9 个 0 + 1 个 90：均值 9，标准差 27，那个 90 的 z 正好等于 3。
    // 判定条件是 |z| > 3，所以边界上的点不该被报成异常。
    qint64 ts = 0;
    for (int i = 0; i < 9; ++i) {
        insertReading(QStringLiteral("rack1"), QStringLiteral("temperature"), 0.0, ts += 1000);
    }
    insertReading(QStringLiteral("rack1"), QStringLiteral("temperature"), 90.0, ts += 1000);

    service_->analyzeSensor(QStringLiteral("rack1"), QStringLiteral("temperature"), 60);

    QVERIFY(completed_);
    QVERIFY(error_.isEmpty());
    QCOMPARE(stats_.count, 10);
    QVERIFY(qAbs(stats_.mean - 9.0) < 1e-9);
    QVERIFY(qAbs(stats_.std - 27.0) < 1e-9);
    QVERIFY(anomalies_.isEmpty());
}

void TstAnalysis::respectsSampleCountLimit() {
    // 按时间插入 1..20，只分析最近 10 条：应该是 11..20
    for (int i = 1; i <= 20; ++i) {
        insertReading(QStringLiteral("rack1"), QStringLiteral("temperature"),
                      static_cast<double>(i), i * 1000);
    }

    service_->analyzeSensor(QStringLiteral("rack1"), QStringLiteral("temperature"), 10);

    QVERIFY(completed_);
    QVERIFY(error_.isEmpty());
    QCOMPARE(stats_.count, 10);          // 上限生效
    QVERIFY(qAbs(stats_.min - 11.0) < 1e-9);   // 旧数据没被算进来
    QVERIFY(qAbs(stats_.max - 20.0) < 1e-9);
}

void TstAnalysis::detectsOutlierBeyondThreeSigma() {
    qint64 ts = 1000;
    for (int i = 0; i < 12; ++i) {
        insertReading(QStringLiteral("rack1"), QStringLiteral("temperature"),
                      20.0, ts += 1000);
    }
    insertReading(QStringLiteral("rack1"), QStringLiteral("temperature"),
                  200.0, ts += 1000);

    service_->analyzeSensor(QStringLiteral("rack1"), QStringLiteral("temperature"), 100);

    QVERIFY(completed_);
    QVERIFY(error_.isEmpty());
    QCOMPARE(stats_.count, 13);
    QCOMPARE(anomalies_.size(), 1);
    QVERIFY(qAbs(anomalies_.first().value - 200.0) < 1e-9);
    QVERIFY(anomalies_.first().zscore > 3.0);   // 正号表示偏高
}

QTEST_GUILESS_MAIN(TstAnalysis)
#include "tst_analysis.moc"