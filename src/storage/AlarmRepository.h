#pragma once

#include "data/AlarmTypes.h"
#include <QVector>
#include <QSqlRecord>

namespace sensorhub {

class Database;

// 用于处理告警规则的查询插入删除
class AlarmRuleRepository {
public:
    explicit AlarmRuleRepository(Database& db) : db_(db) {}
    ~AlarmRuleRepository() {}

    // 返回全部规则
    QVector<AlarmRule> findAll();
    // 返回所有启用的规则
    QVector<AlarmRule> findAllEnable();
    // 新增一条规则，成功后自增主键赋值给 rule.id
    bool add(AlarmRule& rule);
    // 按 id 删除一条规则
    bool removeById(int id);
    // 停用/启用一条规则
    bool setEnabled(int id, bool enabled);
private:
    // 用于：行 -> 结构体
    AlarmRule fromRecord(const QSqlRecord& r) const;

    Database& db_;
};

// 用于写入告警事件
class AlarmEventRepository {
public:
    explicit AlarmEventRepository(Database& db): db_(db) {}
    ~AlarmEventRepository() {}

    // 插入一条新事件，成功后回填 rule.id
    bool add(AlarmEvent& rule);
    // 返回最近几条事件
    QVector<AlarmEvent> findRecent(int limit);
    // 返回处于告警状态下对应规则 id 集合
    QVector<int> findUnresolvedRuleIds();
    // 确认告警
    bool acknowledge(int ruleId);
    // 根据 id 把对应事件设为 resolved
    bool resolveByRule(int ruleId);
private:
    AlarmEvent fromRecord(const QSqlRecord& r) const;

    Database& db_;
};

} // namespace sensorhub