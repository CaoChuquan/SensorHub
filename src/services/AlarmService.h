#pragma once

#include "data/SensorTypes.h"
#include "data/AlarmTypes.h"
#include <QObject>
#include <QVector>
#include <QString>
#include <QHash>

namespace sensorhub {

class Database;
class DataHub;
class AlarmRuleRepository;
class AlarmEventRepository;

class AlarmService : public QObject {
    Q_OBJECT
public:
    AlarmService(Database& db, DataHub& hub, QObject* parent = nullptr);
    ~AlarmService();

    // 用于 UI 查询
    QVector<AlarmRule> rules();    // 返回全部规则
    QVector<AlarmEvent> recentEvents(int limit = 50);  // 最近几条事件

    // 配置变更后发射信号 alarmChanged
    bool addRule(const QString& sensorId, const QString& type,
                 double threshold, AlarmDirection direction);
    bool removeRule(int ruleId);
    bool setRuleEnabled(int ruleId, bool enabled);

    // 确认某条告警：acitve -> acknowledged
    bool acknowledge(int eventId);
signals:
    // 一条新告警被触发
    void alarmTriggered(const AlarmEvent& event);
    // 一条告警数值恢复后自动闭环
    void alarmResolved(const AlarmEvent& event);
    // 规则或事件变更
    void alarmChanged();
private slots:
    void onReading(const SensorReading& reading);
private:
    // 数值是否超过规定的阈值
    bool isViolated(const AlarmRule& rule, double value) const;
    // 重新加载规则到内存并清理已经失效的规则的越线记忆
    void reloadEnableRules();

    AlarmRuleRepository* rule_repo_;
    AlarmEventRepository* event_repo_;

    QVector<AlarmRule> enable_rules_;   // 已启用的规则表
    QHash<int, bool> violation_;        // ruleId 到 是否越界的映射
};

} // namespace sensorhub