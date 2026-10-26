#include "storage/AlarmRepository.h"
#include "storage/Database.h"
#include <QSqlQuery>
#include <QDateTime>

namespace sensorhub {

AlarmRule AlarmRuleRepository::fromRecord(const QSqlRecord& r) const {
    AlarmRule rule;
    rule.id = r.value(QStringLiteral("id")).toInt();
    rule.sensorId = r.value(QStringLiteral("sensor_id")).toString();
    rule.type = r.value(QStringLiteral("type")).toString();
    rule.threshold = r.value(QStringLiteral("threshold")).toDouble();
    rule.direction = static_cast<AlarmDirection>(r.value(QStringLiteral("direction")).toInt());
    rule.enabled = (r.value(QStringLiteral("enabled")).toInt()) != 0;
    rule.createdAtMs = r.value(QStringLiteral("created_at_ms")).toLongLong();
    return rule;
}

QVector<AlarmRule> AlarmRuleRepository::findAll() {
    QVector<AlarmRule> result;
    QSqlQuery q(db_.handle());
    if (!(q.exec(QStringLiteral(
        "select * from alarm_rules order by created_at_ms desc"))))
    {
        return result;
    }

    while(q.next()) result.append(fromRecord(q.record()));

    return result;
}

QVector<AlarmRule> AlarmRuleRepository::findAllEnable() {
    QVector<AlarmRule> result;
    QSqlQuery q(db_.handle());
    if (!(q.exec(QStringLiteral(
        "select * from alarm_rules where enabled = 1 order by created_at_ms desc"))))
    {
        return result;
    }

    while(q.next()) result.append(fromRecord(q.record()));

    return result;
}

bool AlarmRuleRepository::add(AlarmRule& rule) {
    QSqlQuery q(db_.handle());
    // id 交给 AUTOINCREMENT，插入时传 NULL；其余列显式列名，不用 SELECT * 顺序。
    q.prepare(QStringLiteral(
        "INSERT INTO alarm_rules"
        " (sensor_id, type, threshold, direction, enabled, created_at_ms)"
        " VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(rule.sensorId);
    q.addBindValue(rule.type);
    q.addBindValue(rule.threshold);
    q.addBindValue(static_cast<int>(rule.direction));
    q.addBindValue(rule.enabled ? 1 : 0);
    q.addBindValue(rule.createdAtMs != 0
                       ? rule.createdAtMs
                       : QDateTime::currentMSecsSinceEpoch());
    if (!q.exec()) return false;

    // 回填主键
    rule.id = q.lastInsertId().toInt();
    if (rule.createdAtMs == 0) {
        rule.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    }
    return true;
}

bool AlarmRuleRepository::removeById(int id) {
    QSqlQuery q(db_.handle());
    q.prepare(QStringLiteral(
        "delete from alarm_rules where id = ?"));
    q.addBindValue(id);
    return q.exec();
}

bool AlarmRuleRepository::setEnabled(int id, bool enabled) {
    QSqlQuery q(db_.handle());
    q.prepare(QStringLiteral(
        "update alarm_rules set enabled = ? where id = ?"));
    q.addBindValue(enabled ? 1 : 0);
    q.addBindValue(id);
    return q.exec();
}

AlarmEvent AlarmEventRepository::fromRecord(const QSqlRecord& r) const {
    AlarmEvent ev;
    ev.id = r.value(QStringLiteral("id")).toInt();
    ev.ruleId = r.value(QStringLiteral("rule_id")).toInt();
    ev.sensorId = r.value(QStringLiteral("sensor_id")).toString();
    ev.type = r.value(QStringLiteral("type")).toString();
    ev.value = r.value(QStringLiteral("value")).toDouble();
    ev.state = static_cast<AlarmState>(
        r.value(QStringLiteral("state")).toInt());
    ev.triggeredAtMs = r.value(QStringLiteral("triggered_at_ms")).toLongLong();
    ev.resolvedAtMs = r.value(QStringLiteral("resolved_at_ms")).toLongLong();
    return ev;
}

bool AlarmEventRepository::add(AlarmEvent& event) {
    QSqlQuery q(db_.handle());
    q.prepare(QStringLiteral(
        "INSERT INTO alarm_events"
        " (rule_id, sensor_id, type, value, state, triggered_at_ms, resolved_at_ms)"
        " VALUES (?, ?, ?, ?, ?, ?, 0)"));
    q.addBindValue(event.ruleId);
    q.addBindValue(event.sensorId);
    q.addBindValue(event.type);
    q.addBindValue(event.value);
    q.addBindValue(static_cast<int>(event.state));
    q.addBindValue(event.triggeredAtMs);
    if (!q.exec()) return false;

    event.id = q.lastInsertId().toInt();
    return true;
}

QVector<AlarmEvent> AlarmEventRepository::findRecent(int limit) {
    QVector<AlarmEvent> result;
    QSqlQuery q(db_.handle());
    // 排序策略：未处理的排前面，再按触发时间倒序。
    // state 越小越"未处理"（active=0 < acknowledged=1 < resolved=2），
    q.prepare(QStringLiteral(
        "SELECT * FROM alarm_events"
        " ORDER BY state ASC, triggered_at_ms DESC LIMIT ?"));
    q.addBindValue(limit);
    if (!q.exec()) return result;
    while (q.next()) result.append(fromRecord(q.record()));
    return result;
}

QVector<int> AlarmEventRepository::findUnresolvedRuleIds() {
    QVector<int> result;
    QSqlQuery q(db_.handle());
    // state != Resolved 为未处理
    q.prepare(QStringLiteral(
        "SELECT DISTINCT rule_id FROM alarm_events WHERE state != ?"));
    q.addBindValue(static_cast<int>(AlarmState::resolved));
    if (!q.exec()) return result;
    while (q.next()) result.append(q.value(0).toInt());
    return result;
}

bool AlarmEventRepository::acknowledge(int eventId) {
    QSqlQuery q(db_.handle());
    // 带 state = active 条件：已恢复或被重复确认的事件不该被改动，
    q.prepare(QStringLiteral(
        "UPDATE alarm_events SET state = ? WHERE id = ? AND state = ?"));
    q.addBindValue(static_cast<int>(AlarmState::acknowledged));
    q.addBindValue(eventId);
    q.addBindValue(static_cast<int>(AlarmState::active));
    if (!q.exec()) return false;
    // 用影响行数而不是 exec() 的返回值：没有命中任何行时 exec() 也是 true，
    // 但业务上"没确认成功"。UI 依赖这个区分给出准确提示。
    return q.numRowsAffected() > 0;
}

bool AlarmEventRepository::resolveByRule(int ruleId) {
    QSqlQuery q(db_.handle());
    q.prepare(QStringLiteral(
        "UPDATE alarm_events SET state = ?, resolved_at_ms = ?"
        " WHERE rule_id = ? AND state IN (?, ?)"));
    q.addBindValue(static_cast<int>(AlarmState::resolved));
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.addBindValue(ruleId);
    q.addBindValue(static_cast<int>(AlarmState::active));
    q.addBindValue(static_cast<int>(AlarmState::acknowledged));
    if (!q.exec()) return false;
    // 只有确实闭环掉了事件才返回 true，Service 据此决定是否广播恢复
    return q.numRowsAffected() > 0;
}

} // namespace sensorhub