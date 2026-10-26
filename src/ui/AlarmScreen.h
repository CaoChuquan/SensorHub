#pragma once

#include "data/AlarmTypes.h"
#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>

namespace sensorhub {

class AlarmService;

// 告警面板类
class AlarmScreen : public QWidget {
    Q_OBJECT
public:
    explicit AlarmScreen(AlarmService& service, QWidget* parent = nullptr);
    ~AlarmScreen() {}
private slots:
    // 响应用户操作
    void onAddRule();
    void onRemoveRule();
    void onToggleRule();
    void onAcknowledge();

    // 服务变更响应
    void onAlarmChanged();
    void onAlarmTriggered(const AlarmEvent& ev);
    void onAlarmResolved(const AlarmEvent& ev);
private:
    void setupUi();
    void reloadRulesTable();
    void reloadEventsTable();

    int currentRuleId() const;    // 返回当前选中的规则 id
    int currentEventId() const;   // 返回当前选中的告警 id

    QTableWidget* tableRules_ = nullptr;
    QLineEdit* editSensorId_ = nullptr;
    QComboBox* comboType_ = nullptr;
    QLineEdit* editThreshold_ = nullptr;
    QComboBox* comboDirection_ = nullptr;
    QPushButton* btnAdd_ = nullptr;
    QPushButton* btnRemove_ = nullptr;
    QPushButton* btnToggle_  = nullptr;

    QLabel* lblBanner_ = nullptr;  // 顶部提示条：最近一次报警/恢复
    QTableWidget* tableEvents_ = nullptr;
    QPushButton* btnAck_ = nullptr;
    QPushButton* btnRefresh_ = nullptr;

    AlarmService& service_;
};

} // namespace sensorhub