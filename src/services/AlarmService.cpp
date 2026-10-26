#include "services/AlarmService.h"
#include "storage/Database.h"
#include "storage/AlarmRepository.h"
#include "core/DataHub.h"
#include <QDateTime>
#include <QDebug>

namespace sensorhub {

AlarmService::AlarmService(Database& db, DataHub& hub, QObject* parent) 
    : QObject(parent)
    , rule_repo_(new AlarmRuleRepository(db))
    , event_repo_(new AlarmEventRepository(db))
{
    // 先找出未处理的告警 id，下次重启时可立即看到未处理的告警
    const QVector<int> unresolvedids = event_repo_->findUnresolvedRuleIds();
    for (int id : unresolvedids) {
       violation_.insert(id, true);  // 标记为越界
    }

    // 载入启用的规则
    reloadEnableRules();

    // 订阅数据平面
    connect(&hub, &DataHub::readingReceived,
            this, &AlarmService::onReading);
}

AlarmService::~AlarmService() {
    delete rule_repo_;
    delete event_repo_;
}

QVector<AlarmRule> AlarmService::rules() {
    return rule_repo_->findAll();
}   

QVector<AlarmEvent> AlarmService::recentEvents(int limit) {
    return event_repo_->findRecent(limit);
}

bool AlarmService::addRule(const QString& sensorId, const QString& type,
                 double threshold, AlarmDirection direction)
{
    AlarmRule rule(sensorId, type, threshold, 
        direction, QDateTime::currentMSecsSinceEpoch());
    if (!rule_repo_->add(rule)) { // 规则持久化
        qWarning() << "[AlarmService] 新增规则失败" << sensorId << type;
        return false;
    }
    // 加入新规则后必须立即进入判定集合
    reloadEnableRules();
    emit alarmChanged();    // 发射信号
    return true;
}

bool AlarmService::removeRule(int id) {
    // 把对应事件改成已解决
    event_repo_->resolveByRule(id);
    // 把对应规则删除
    if (!rule_repo_->removeById(id)) return false;

    // 更新内存缓存
    reloadEnableRules();
    emit alarmChanged();
    return true;
}

bool AlarmService::setRuleEnabled(int id, bool enabled) {
    if (!rule_repo_->setEnabled(id, enabled)) return false;

    // 如果是停用这个规则，那么此规则对应的告警要随之闭环
    if (!enabled) {
        event_repo_->resolveByRule(id);
    }

    reloadEnableRules();
    emit alarmChanged();
    return true;
}

bool AlarmService::acknowledge(int id) {
    if (event_repo_->acknowledge(id)) {
        reloadEnableRules();
        emit alarmChanged();
        return true;
    }
    return false;
}

void AlarmService::reloadEnableRules() {
    // 更新规则表
    enable_rules_ = rule_repo_->findAllEnable();
    // 更新映射
    QHash<int, bool> kept;
    for (const auto& rule : enable_rules_) {
        if (violation_.value(rule.id, false)) {
            kept.insert(rule.id, true);
        }
    }
    violation_ = kept;
}

bool AlarmService::isViolated(const AlarmRule& rule, double value) const {
    if (rule.direction == AlarmDirection::above) {
        return rule.threshold < value;
    }
    return rule.threshold > value;
}

void AlarmService::onReading(const SensorReading& reading) {
    for (const auto& rule : enable_rules_) {
        // 同时定位机柜 id 和指标
        if ((rule.sensorId == reading.sensorId) && (rule.type == reading.type)) {
            // rule 这次有没有被违反
            const bool violated =  isViolated(rule, reading.value);
            // rule 之前有没有被违法
            const bool was_violated = violation_.value(rule.id, false);

            if (violated && !was_violated) {
                // 之前正常，这次超标了 -> 记录一条新告警
                AlarmEvent ev;
                ev.ruleId = rule.id;
                ev.sensorId = reading.sensorId;
                ev.type = reading.type;
                ev.value = reading.value;
                ev.state = AlarmState::active;
                ev.triggeredAtMs = reading.timestampMs;

                if (event_repo_->add(ev)) {
                    // 存库成功后就更新 violation_
                    violation_.insert(rule.id, true);
                    qDebug() << "[AlarmService] 触发告警" << rule.sensorId << rule.type
                                << (rule.direction == AlarmDirection::above ? ">" : "<")
                                << rule.threshold << "当前" << reading.value;
                    emit alarmTriggered(ev);
                    emit alarmChanged();
                }
                else {
                    // 存库没成功，如果下一次这个指标的读数依旧越界那就会再试一次，
                    // 如果这个指标的读数恢复正常那上一次越界的读数对应告警丢失
                    qWarning() << "[AlarmService] 告警写库失败，等下一条读数重试"
                               << rule.sensorId << rule.type;
                }
            }
            else if (!violated && was_violated) {
                // 之前超标，这次恢复正常了，标记为已恢复
                if (event_repo_->resolveByRule(rule.id)) {
                    violation_.insert(rule.id, false);

                    // 给界面发送一条已经恢复的通知
                    AlarmEvent ev;
                    ev.ruleId = rule.id;
                    ev.sensorId = reading.sensorId;
                    ev.type = reading.type;
                    ev.value = reading.value;
                    ev.state = AlarmState::active;
                    ev.triggeredAtMs = reading.timestampMs;
                    ev.resolvedAtMs = QDateTime::currentMSecsSinceEpoch();
                    qDebug() << "[AlarmService] 告警恢复" << rule.sensorId << rule.type;
                    emit alarmResolved(ev);
                    emit alarmChanged();
                }
            }
            // 其他两种情况：一直正常 -> 不用管。一直越界 -> 不重复告警
        }
    }
}

} // namespace sensorhub