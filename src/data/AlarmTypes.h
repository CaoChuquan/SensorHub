#pragma once

#include <QString>
#include <QMetaType>

namespace sensorhub {

enum class AlarmDirection {
    above = 0,  // 高于阈值触发
    below = 1   // 低于阈值触发
};

// 告警事件状态
enum class AlarmState {
    active = 0,         // 已触发，等待处理
    acknowledged = 1,   // 用户已确认知晓
    resolved = 2        // 数值已回到正常区间，系统自动关闭
};

struct AlarmRule {
    int id = 0;              // 数据库自增主键，0 表示尚未持久化
    QString sensorId;        // 目标机柜，如 "rack1"
    QString type;            // 目标指标：temperature / humidity / voltage
    double threshold = 0.0;  // 阈值
    AlarmDirection direction = AlarmDirection::above;  // 越限方向
    bool enabled = true;     // 停用后规则仍保留在列表中，但不参与判定
    qint64 createdAtMs = 0;  // 创建时间（毫秒 epoch）

    AlarmRule() = default;
    AlarmRule(QString sensor, QString t, double thr,
              AlarmDirection dir, qint64 ts)
        : sensorId(std::move(sensor)), type(std::move(t)),
        threshold(thr), direction(dir), createdAtMs(ts) {}
};

// 规则被违反时生成的一条“报警记录”,同一规则在“一次越限全程”里只产生一条
struct AlarmEvent {
    int id = 0;                       // 数据库自增主键，0 表示尚未持久化
    int ruleId = 0;                   // 关联的规则 id（规则删除后此值可能悬空，
    // 历史事件作为一种"审计痕迹"予以保留）
    QString sensorId;                 // 冗余存目标机柜，便于事件列表独立展示
    QString type;                     // 冗余存指标类型，同上
    double value = 0.0;               // 触发时的读数（越限严重程度的证据）
    AlarmState state = AlarmState::active;
    qint64 triggeredAtMs = 0;         // 触发时刻
    qint64 resolvedAtMs = 0;          // 恢复时刻，0 表示尚未恢复

    AlarmEvent() = default;
};

} // namespace sensorhub

// 注册元类型
Q_DECLARE_METATYPE(sensorhub::AlarmRule);