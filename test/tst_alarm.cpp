/*
告警模块的单元测试

测试方式：
  不启动界面、不连网络，直接往 DataHub 里 publish 一条读数，    
  然后检查数据库里生成了几条事件、状态是什么
*/
#include <QTest>

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
    // 用例 4：确认之后读数仍在超标，不应再产生新事件
    void acknowledgeDoesNotRetrigger();
    // 用例 5：重启后若仍在超标，不应把同一次故障当成新警再报一遍
    void restartRestoresViolationState();
    // 用例 6：停用规则时，它名下未结束的事件应一起收尾
    void disablingRuleResolvesItsEvents();
private:
    Database* db_ = nullptr;
    DataHub* hub_ = nullptr;
    AlarmService* service_ = nullptr;
    qint64 now_ = 0;
};

void TstAlarm::init() {
    now_ = 1000;

    // 内存库：每个用例一开一关，互不影响，也不会碰到开发用的 sensorhub.db
    // 注意":memory:" 的库随连接存在，所以"重启恢复"用例只重建服务、不重建 Database
    db_ = new Database;
    QVERIFY2(db_->open(QStringLiteral(":memory:")), qPrintable(db_->lastError()));

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

void TstAlarm::acknowledgeDoesNotRetrigger() {
    QVERIFY(service_->addRule(QStringLiteral("rack1"), QStringLiteral("temperature"),
                              30.0, AlarmDirection::above));

    hub_->publish(makeReading(QStringLiteral("rack1"), QStringLiteral("temperature"),
                              35.0, now_ += 1000));

    QVector<AlarmEvent> events = service_->recentEvents(10);
    QCOMPARE(events.size(), 1);
    QVERIFY(service_->acknowledge(events.first().id));

    // 人点了"确认"，但温度还没降下来：此时不应该再报一条新警
    hub_->publish(makeReading(QStringLiteral("rack1"), QStringLiteral("temperature"),
                              36.0, now_ += 1000));

    events = service_->recentEvents(10);
    QCOMPARE(events.size(), 1);
    QCOMPARE(static_cast<int>(events.first().state),
             static_cast<int>(AlarmState::acknowledged));
}

void TstAlarm::restartRestoresViolationState() {
    QVERIFY(service_->addRule(QStringLiteral("rack1"), QStringLiteral("temperature"),
                              30.0, AlarmDirection::above));
    hub_->publish(makeReading(QStringLiteral("rack1"), QStringLiteral("temperature"),
                              35.0, now_ += 1000));
    QCOMPARE(service_->recentEvents(10).size(), 1);

    // 模拟程序重启：服务与 hub 全部重建，只保留数据库
    delete service_;
    service_ = nullptr;
    delete hub_;
    hub_ = nullptr;
    hub_ = new DataHub;
    service_ = new AlarmService(*db_, *hub_);

    // 重启后温度仍超标。若没从库里恢复"正在越限"的记忆，这里会再报一条。
    hub_->publish(makeReading(QStringLiteral("rack1"), QStringLiteral("temperature"),
                              36.0, now_ += 1000));

    QCOMPARE(service_->recentEvents(10).size(), 1);
}

void TstAlarm::disablingRuleResolvesItsEvents() {
    QVERIFY(service_->addRule(QStringLiteral("rack1"), QStringLiteral("temperature"),
                              30.0, AlarmDirection::above));
    hub_->publish(makeReading(QStringLiteral("rack1"), QStringLiteral("temperature"),
                              35.0, now_ += 1000));

    const QVector<AlarmRule> rules = service_->rules();
    QCOMPARE(rules.size(), 1);
    QVERIFY(service_->setRuleEnabled(rules.first().id, false));

    // 规则停用等于"别再盯这个点了"，它名下还挂着的告警不该永远停在"未处理"
    const QVector<AlarmEvent> events = service_->recentEvents(10);
    QCOMPARE(events.size(), 1);
    QCOMPARE(static_cast<int>(events.first().state),
             static_cast<int>(AlarmState::resolved));
}

QTEST_GUILESS_MAIN(TstAlarm)
#include "tst_alarm.moc"