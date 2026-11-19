// 存储层仓储的单元测试
// 为什么测这里：
//   仓储里的 SQL 是"手写字符串"，拼错一个列名、漏掉一个 WHERE 条件，
//   编译完全发现不了，只会在界面上表现为"关注列表多出一条""_history 少一段"
//   用测试把 SQL 的语义钉死，以后改表结构时能第一时间发现回归
// 测的两个仓储：
//   WatchlistRepository —— 用户配置的读写（增 / 查 / 删 / 重复添加的替换语义）
//   SensorRepository    —— 历史读数的"最近 N 条"查询（过滤 + 限量 + 时间正序）
#include <QSqlError>
#include <QSqlQuery>
#include <QTest>

#include "data/SensorTypes.h"
#include "storage/Database.h"
#include "storage/SensorRepository.h"
#include "storage/WatchlistRepository.h"

using namespace sensorhub;

class TstStorage : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    // 关注列表：添加后能读出来
    void watchlistAddThenLoad();
    // 关注列表：同一 sensorId+type 重复添加只保留一条，label 被更新
    void watchlistDuplicateReplacesLabel();
    // 关注列表：按 id 删除
    void watchlistRemoveById();
    // 关注列表：加载结果按添加时间升序（与插入顺序无关）
    void watchlistLoadsInAddedTimeOrder();
    // 关注列表：删除不存在的 id 不报错、也不误删别人
    void watchlistRemoveUnknownIdIsNoop();
    // 读数仓储：按 sensorId + type 过滤，不混入其它指标和其它机柜
    void findRecentFiltersBySensorAndType();
    // 读数仓储：只取最新 N 条，且返回结果是时间正序
    void findRecentLimitsAndSortsAscending();
private:
    // 直接插历史读数，构造测试数据
    void insertReading(const QString& sensorId, const QString& type,
                       double value, qint64 timestampMs);

    Database* db_ = nullptr;
};

void TstStorage::init() {
    // 内存库：每个用例都是全新的空库，不落盘
    db_ = new Database;
    QVERIFY2(db_->open(QStringLiteral(":memory:")), qPrintable(db_->lastError()));
}

void TstStorage::cleanup() {
    delete db_;
    db_ = nullptr;
}

void TstStorage::insertReading(const QString& sensorId, const QString& type,
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

void TstStorage::watchlistAddThenLoad() {
    WatchlistRepository repo(*db_);

    QVERIFY(repo.add(WatchlistEntry(QStringLiteral("rack1"), QStringLiteral("temperature"),
                                    QStringLiteral("1 号机柜进风口"), 1000)));

    const QVector<WatchlistEntry> all = repo.loadAll();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.first().sensorId, QStringLiteral("rack1"));
    QCOMPARE(all.first().type, QStringLiteral("temperature"));
    QCOMPARE(all.first().label, QStringLiteral("1 号机柜进风口"));
    QCOMPARE(all.first().addedAtMs, 1000LL);
    QVERIFY(all.first().id > 0);
}

void TstStorage::watchlistDuplicateReplacesLabel() {
    WatchlistRepository repo(*db_);

    QVERIFY(repo.add(WatchlistEntry(QStringLiteral("rack1"), QStringLiteral("temperature"),
                                    QStringLiteral("旧标签"), 1000)));
    // 同一个 sensorId + type，只改标签
    QVERIFY(repo.add(WatchlistEntry(QStringLiteral("rack1"), QStringLiteral("temperature"),
                                    QStringLiteral("新标签"), 2000)));

    const QVector<WatchlistEntry> all = repo.loadAll();
    QCOMPARE(all.size(), 1);   // UNIQUE 约束生效，没多出一条
    QCOMPARE(all.first().label, QStringLiteral("新标签"));
    QCOMPARE(all.first().addedAtMs, 2000LL);
}

void TstStorage::watchlistRemoveById() {
    WatchlistRepository repo(*db_);

    QVERIFY(repo.add(WatchlistEntry(QStringLiteral("rack1"), QStringLiteral("temperature"),
                                    QString(), 1000)));
    const int id = repo.loadAll().first().id;

    QVERIFY(repo.removeById(id));
    QVERIFY(repo.loadAll().isEmpty());
}

void TstStorage::watchlistLoadsInAddedTimeOrder() {
    WatchlistRepository repo(*db_);

    // 故意乱序添加：加载时应该按添加时间升序，而不是插入顺序
    QVERIFY(repo.add(WatchlistEntry(QStringLiteral("rack3"), QStringLiteral("voltage"),
                                    QString(), 3000)));
    QVERIFY(repo.add(WatchlistEntry(QStringLiteral("rack1"), QStringLiteral("temperature"),
                                    QString(), 1000)));
    QVERIFY(repo.add(WatchlistEntry(QStringLiteral("rack2"), QStringLiteral("humidity"),
                                    QString(), 2000)));

    const QVector<WatchlistEntry> all = repo.loadAll();
    QCOMPARE(all.size(), 3);
    QCOMPARE(all.at(0).addedAtMs, 1000LL);
    QCOMPARE(all.at(1).addedAtMs, 2000LL);
    QCOMPARE(all.at(2).addedAtMs, 3000LL);
}

void TstStorage::watchlistRemoveUnknownIdIsNoop() {
    WatchlistRepository repo(*db_);

    QVERIFY(repo.add(WatchlistEntry(QStringLiteral("rack1"), QStringLiteral("temperature"),
                                    QString(), 1000)));

    // 删一个根本不存在的 id：SQL 执行成功（影响 0 行），不应报错、更不该误删
    QVERIFY(repo.removeById(99999));
    QCOMPARE(repo.loadAll().size(), 1);
}

void TstStorage::findRecentFiltersBySensorAndType() {
    // 混入三种数据：目标数据、其它指标、其它机柜
    insertReading(QStringLiteral("rack1"), QStringLiteral("temperature"), 25.0, 1000);
    insertReading(QStringLiteral("rack1"), QStringLiteral("humidity"),    50.0, 2000);
    insertReading(QStringLiteral("rack2"), QStringLiteral("temperature"), 30.0, 3000);

    SensorRepository repo(*db_);
    const QVector<SensorReading> rows =
        repo.findRecent(QStringLiteral("rack1"), QStringLiteral("temperature"), 10);

    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().sensorId, QStringLiteral("rack1"));
    QCOMPARE(rows.first().type, QStringLiteral("temperature"));
    QVERIFY(qAbs(rows.first().value - 25.0) < 1e-9);
}

void TstStorage::findRecentLimitsAndSortsAscending() {
    // 5 条按时间递增插入，只取最近 3 条：应是 3000 / 4000 / 5000，并按正序返回
    for (int i = 1; i <= 5; ++i) {
        insertReading(QStringLiteral("rack1"), QStringLiteral("temperature"),
                      static_cast<double>(i), i * 1000);
    }

    SensorRepository repo(*db_);
    const QVector<SensorReading> rows =
        repo.findRecent(QStringLiteral("rack1"), QStringLiteral("temperature"), 3);

    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows.at(0).timestampMs, 3000LL);
    QCOMPARE(rows.at(1).timestampMs, 4000LL);
    QCOMPARE(rows.at(2).timestampMs, 5000LL);
    QVERIFY(qAbs(rows.at(0).value - 3.0) < 1e-9);
    QVERIFY(qAbs(rows.at(2).value - 5.0) < 1e-9);
}

QTEST_GUILESS_MAIN(TstStorage)
#include "tst_storage.moc"