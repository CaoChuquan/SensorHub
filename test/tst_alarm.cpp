/*
告警模块的单元测试

测试方式：
  不启动界面、不连网络，直接往 DataHub 里 publish 一条读数，    
  然后检查数据库里生成了几条事件、状态是什么
*/
#include <QTest>
#include <QTemporaryDir>

#include "core/DataHub.h"
#include "data/AlarmTypes.h"
#include "data/SensorTypes.h"
#include "services/AlarmService.h"
#include "storage/Database.h"

using namespace sensorhub;

namespace {

// 造一条读数，省得每个用例都写四行赋值
SensorReading makeReading(const QString& sensorId, const QString& type,
                          double value, qint64 timestampMs) {
    SensorReading r;
    r.sensorId = sensorId;
    r.type = type;
    r.value = value;
    r.timestampMs = timestampMs;
    return r;
}

} // namespace

class TstAlarm : public QObject {
    Q_OBJECT
private slots:
    // 每个用例前重建一套干净的对象：临时数据库 + DataHub + AlarmService
    void init();
    void cleanup();

    // 用例 1：从正常变成超标，应该产生且只产生 1 条事件
    void firstViolationProducesOneEvent();

    // 用例 2：持续超标（连续多条读数），不应该重复报警
    void continuousViolationDoesNotRepeat();

    // 用例 3：恢复正常后，事件应被自动标记为"已恢复"
    void recoveryResolvesEvent();
private:
    QTemporaryDir tmpDir_;
    Database* db_ = nullptr;
    DataHub* hub_ = nullptr;
    AlarmService* service_ = nullptr;
    qint64 now_ = 0;
};

void TstAlarm::init() {
    now_ = 1000;

    // 每个用例一个独立的库文件，互不影响，也不会碰到开发用的 sensorhub.db
    const QString dbPath = tmpDir_.filePath(
        QStringLiteral("%1.db").arg(QString::fromLatin1(QTest::currentTestFunction())));

    db_ = new Database;
    QVERIFY2(db_->open(dbPath), qPrintable(db_->lastError()));

    hub_ = new DataHub;
    service_ = new AlarmService(*db_, *hub_);
}

void TstAlarm::cleanup() {
    // 析构顺序很重要：AlarmService 里持有仓储对象，仓储持有数据库连接，
    // 必须先删服务，再删库。
    delete service_;
    service_ = nullptr;
    delete hub_;
    hub_ = nullptr;
    delete db_;
    db_ = nullptr;
}

void TstAlarm::firstViolationProducesOneEvent() {
    QVERIFY(service_->addRule(QStringLiteral("rack1"), QStringLiteral("temperature"),
                              30.0, AlarmDirection::above));

    // 29.9 不超标，35 超标 —— 这正是"从正常变成超标"的那一刻
    hub_->publish(makeReading(QStringLiteral("rack1"), QStringLiteral("temperature"),
                              29.9, now_ += 1000));
    QCOMPARE(service_->recentEvents(10).size(), 0);

    hub_->publish(makeReading(QStringLiteral("rack1"), QStringLiteral("temperature"),
                              35.0, now_ += 1000));

    const QVector<AlarmEvent> events = service_->recentEvents(10);
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.first().sensorId, QStringLiteral("rack1"));
    QCOMPARE(events.first().type, QStringLiteral("temperature"));
    QVERIFY(qAbs(events.first().value - 35.0) < 1e-9);
    QCOMPARE(static_cast<int>(events.first().state),
             static_cast<int>(AlarmState::active));
}

void TstAlarm::continuousViolationDoesNotRepeat() {
    QVERIFY(service_->addRule(QStringLiteral("rack1"), QStringLiteral("temperature"),
                              30.0, AlarmDirection::above));

    // 连续 5 条都超标：只在第一次触发，后面 4 条不应该再建事件
    for (int i = 0; i < 5; ++i) {
        hub_->publish(makeReading(QStringLiteral("rack1"), QStringLiteral("temperature"),
                                  31.0 + i, now_ += 1000));
    }

    QCOMPARE(service_->recentEvents(10).size(), 1);
}

void TstAlarm::recoveryResolvesEvent() {
    QVERIFY(service_->addRule(QStringLiteral("rack1"), QStringLiteral("temperature"),
                              30.0, AlarmDirection::above));

    hub_->publish(makeReading(QStringLiteral("rack1"), QStringLiteral("temperature"),
                              35.0, now_ += 1000));
    hub_->publish(makeReading(QStringLiteral("rack1"), QStringLiteral("temperature"),
                              20.0, now_ += 1000));

    const QVector<AlarmEvent> events = service_->recentEvents(10);
    QCOMPARE(events.size(), 1);   // 恢复不应该新增一条，而是把原来那条收尾
    QCOMPARE(static_cast<int>(events.first().state),
             static_cast<int>(AlarmState::resolved));
    QVERIFY(events.first().resolvedAtMs > 0);
}

QTEST_GUILESS_MAIN(TstAlarm)
#include "tst_alarm.moc"