#include "ui/AlarmScreen.h"
#include "services/AlarmService.h"
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QDateTime>
#include <QMessageBox>

namespace sensorhub {

namespace {

// 表格列索引
enum RuleCol { RuleId = 0, RuleSensor, RuleType, RuleThreshold,
               RuleDirection, RuleEnabled, RuleColCount };
enum EventCol { EvTime = 0, EvSensor, EvType, EvValue,
                EvState, EvResolved, EvColCount };

constexpr int kEventLimit = 200;

// 根据指标类型转为中文显示名
QString typeToDisplay(const QString& type) {
    if (type == QLatin1String("temperature")) return QStringLiteral("温度");
    if (type == QLatin1String("humidity")) return QStringLiteral("湿度");
    if (type == QLatin1String("voltage")) return QStringLiteral("电压");
    return type;
}

// 指标类型 → 单位
QString unitForType(const QString& type) {
    if (type == QLatin1String("temperature")) return QStringLiteral("°C");
    if (type == QLatin1String("humidity")) return QStringLiteral("%RH");
    if (type == QLatin1String("voltage")) return QStringLiteral("V");
    return {};
}

QString directionToDisplay(AlarmDirection d) {
    return d == AlarmDirection::above ? QStringLiteral("高于阈值") : QStringLiteral("低于阈值");
}

QString stateToDisplay(AlarmState state) {
    switch(state) {
        case AlarmState::active: return QStringLiteral("未处理");
        case AlarmState::acknowledged: return QStringLiteral("已确定");
        case AlarmState::resolved: return QStringLiteral("已恢复");
    }
    return {};
}

QColor stateColor(AlarmState state) {
    switch (state) {
    case AlarmState::active:       return QColor(QStringLiteral("#c0392b"));
    case AlarmState::acknowledged: return QColor(QStringLiteral("#e67e22"));
    case AlarmState::resolved:     return QColor(QStringLiteral("#7f8c8d"));
    }
    return QColor(Qt::black);
}

} // namespace

AlarmScreen::AlarmScreen(AlarmService& service, QWidget* parent) 
    : QWidget(parent), service_(service)
{
    setupUi();

    // 服务端一有变化就更新表格
    connect(&service_, &AlarmService::alarmChanged, 
            this, &AlarmScreen::onAlarmChanged);
    connect(&service_, &AlarmService::alarmTriggered,
            this, &AlarmScreen::onAlarmTriggered);
    connect(&service_, &AlarmService::alarmResolved,
            this, &AlarmScreen::onAlarmResolved);

    // 先展示已存在的规则和事件
    reloadRulesTable();
    reloadEventsTable();
}

void AlarmScreen::setupUi() {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // 左：规则配置区
    auto* rulesBox = new QGroupBox(QStringLiteral("告警规则"), this);
    auto* rulesLayout = new QVBoxLayout(rulesBox);

    tableRules_ = new QTableWidget(0, RuleColCount, rulesBox);
    tableRules_->setHorizontalHeaderLabels({
        QStringLiteral("ID"),     QStringLiteral("机柜"),
        QStringLiteral("指标"),   QStringLiteral("阈值"),
        QStringLiteral("方向"),   QStringLiteral("状态"),
    });
    tableRules_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableRules_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableRules_->setSelectionMode(QAbstractItemView::SingleSelection);
    tableRules_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tableRules_->verticalHeader()->setVisible(false);
    rulesLayout->addWidget(tableRules_, 1);

    // 新增规则输入行
    auto* addRow = new QHBoxLayout();
    addRow->addWidget(new QLabel(QStringLiteral("机柜:"), rulesBox));
    editSensorId_ = new QLineEdit(QStringLiteral("rack1"), rulesBox);
    editSensorId_->setMaximumWidth(90);
    addRow->addWidget(editSensorId_);

    addRow->addWidget(new QLabel(QStringLiteral("指标:"), rulesBox));
    comboType_ = new QComboBox(rulesBox);
    // 中文显示、英文内部值（与库里存的取值一致）
    comboType_->addItem(QStringLiteral("温度"), QStringLiteral("temperature"));
    comboType_->addItem(QStringLiteral("湿度"), QStringLiteral("humidity"));
    comboType_->addItem(QStringLiteral("电压"), QStringLiteral("voltage"));
    addRow->addWidget(comboType_);

    addRow->addWidget(new QLabel(QStringLiteral("阈值:"), rulesBox));
    editThreshold_ = new QLineEdit(QStringLiteral("30"), rulesBox);
    editThreshold_->setMaximumWidth(70);
    // 阈值可正可负（如零下温度告警），范围给宽一些，只挡明显离谱的输入
    editThreshold_->setValidator(new QDoubleValidator(-100000.0, 100000.0,
                                                     2, this));
    addRow->addWidget(editThreshold_);

    addRow->addWidget(new QLabel(QStringLiteral("方向:"), rulesBox));
    comboDirection_ = new QComboBox(rulesBox);
    // 用 userData 携带枚举值，避免靠"第几项"的隐式约定
    comboDirection_->addItem(QStringLiteral("高于"), 
                             static_cast<int>(AlarmDirection::above));
    comboDirection_->addItem(QStringLiteral("低于"),
                             static_cast<int>(AlarmDirection::below));
    addRow->addWidget(comboDirection_);

    btnAdd_ = new QPushButton(QStringLiteral("新增规则"), rulesBox);
    addRow->addWidget(btnAdd_);
    addRow->addStretch();
    rulesLayout->addLayout(addRow);

    // 规则操作行
    auto* ruleOpRow = new QHBoxLayout();
    btnRemove_ = new QPushButton(QStringLiteral("删除选中"), rulesBox);
    btnToggle_ = new QPushButton(QStringLiteral("启用/停用选中"), rulesBox);
    ruleOpRow->addWidget(btnRemove_);
    ruleOpRow->addWidget(btnToggle_);
    ruleOpRow->addStretch();
    rulesLayout->addLayout(ruleOpRow);

    layout->addWidget(rulesBox, 1);

    // 右：告警事件区
    auto* eventsBox = new QGroupBox(QStringLiteral("告警事件"), this);
    auto* eventsLayout = new QVBoxLayout(eventsBox);

    // 顶部提示条：显示最近一次报警或恢复。
    // 这里用一行会变色的文字，而不是弹窗——因为告警可能一个接一个来，
    // 弹窗会不停打断用户正在做的事。
    lblBanner_ = new QLabel(QStringLiteral("暂无告警"), eventsBox);
    lblBanner_->setWordWrap(true);
    lblBanner_->setStyleSheet(QStringLiteral(
        "padding: 6px; border: 1px solid #ccc; background: #f7f7f7;"));
    eventsLayout->addWidget(lblBanner_);

    tableEvents_ = new QTableWidget(0, EvColCount, eventsBox);
    tableEvents_->setHorizontalHeaderLabels({
        QStringLiteral("触发时间"), QStringLiteral("机柜"),
        QStringLiteral("指标"), QStringLiteral("数值"),
        QStringLiteral("状态"), QStringLiteral("恢复时间"),
    });
    tableEvents_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableEvents_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableEvents_->setSelectionMode(QAbstractItemView::SingleSelection);
    tableEvents_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tableEvents_->verticalHeader()->setVisible(false);
    eventsLayout->addWidget(tableEvents_, 1);

    auto* eventOpRow = new QHBoxLayout();
    btnAck_ = new QPushButton(QStringLiteral("确认选中告警"), eventsBox);
    btnRefresh_ = new QPushButton(QStringLiteral("刷新"), eventsBox);
    eventOpRow->addWidget(btnAck_);
    eventOpRow->addWidget(btnRefresh_);
    eventOpRow->addStretch();
    eventsLayout->addLayout(eventOpRow);

    layout->addWidget(eventsBox, 1);

    // 信号槽 
    connect(btnAdd_, &QPushButton::clicked, this, &AlarmScreen::onAddRule);
    connect(btnRemove_, &QPushButton::clicked, this, &AlarmScreen::onRemoveRule);
    connect(btnToggle_, &QPushButton::clicked, this, &AlarmScreen::onToggleRule);
    connect(btnAck_, &QPushButton::clicked, this, &AlarmScreen::onAcknowledge);
    connect(btnRefresh_, &QPushButton::clicked,
            this, &AlarmScreen::reloadEventsTable);
}

void AlarmScreen::reloadRulesTable() {
    const QVector<AlarmRule> rules = service_.rules();
    tableRules_->setRowCount(rules.size());

    for (int row = 0; row < rules.size(); ++row) {
        const AlarmRule& r = rules[row];

        // 编号额外存一份在 UserRole 里：用户删掉一行之后，行号会变，
        // 但挂在行上的编号不会变，取"选中的是哪条"时不会拿错。
        auto* idItem = new QTableWidgetItem(QString::number(r.id));
        idItem->setData(Qt::UserRole, r.id);
        tableRules_->setItem(row, RuleId, idItem);

        tableRules_->setItem(row, RuleSensor,
                             new QTableWidgetItem(r.sensorId));
        tableRules_->setItem(row, RuleType,
                             new QTableWidgetItem(typeToDisplay(r.type)));

        const QString unit = unitForType(r.type);
        const QString thrText = unit.isEmpty()
            ? QString::number(r.threshold, 'f', 2)
            : QStringLiteral("%1 %2").arg(QString::number(r.threshold, 'f', 2),
                                          unit);
        tableRules_->setItem(row, RuleThreshold, new QTableWidgetItem(thrText));

        tableRules_->setItem(row, RuleDirection,
                             new QTableWidgetItem(directionToDisplay(r.direction)));

        auto* enabledItem = new QTableWidgetItem(
            r.enabled ? QStringLiteral("启用") : QStringLiteral("停用"));
        enabledItem->setForeground(QBrush(r.enabled
            ? QColor(QStringLiteral("#27ae60"))
            : QColor(QStringLiteral("#7f8c8d"))));
        tableRules_->setItem(row, RuleEnabled, enabledItem);
    }   
}

void AlarmScreen::reloadEventsTable() {
    const QVector<AlarmEvent> events = service_.recentEvents(kEventLimit);
    tableEvents_->setRowCount(events.size());

    for (int row = 0; row < events.size(); ++row) {
        const AlarmEvent& e = events[row];

        auto* timeItem = new QTableWidgetItem(
            QDateTime::fromMSecsSinceEpoch(e.triggeredAtMs)
                .toString(QStringLiteral("MM-dd HH:mm:ss")));
        timeItem->setData(Qt::UserRole, e.id);
        tableEvents_->setItem(row, EvTime, timeItem);

        tableEvents_->setItem(row, EvSensor,
                              new QTableWidgetItem(e.sensorId));
        tableEvents_->setItem(row, EvType,
                              new QTableWidgetItem(typeToDisplay(e.type)));

        const QString unit = unitForType(e.type);
        const QString valText = unit.isEmpty()
            ? QString::number(e.value, 'f', 2)
            : QStringLiteral("%1 %2").arg(QString::number(e.value, 'f', 2), unit);
        tableEvents_->setItem(row, EvValue, new QTableWidgetItem(valText));

        // 状态列染色，未处理的告警最醒目
        auto* stateItem = new QTableWidgetItem(stateToDisplay(e.state));
        stateItem->setForeground(QBrush(stateColor(e.state)));
        tableEvents_->setItem(row, EvState, stateItem);

        const QString resolvedText = e.resolvedAtMs > 0
            ? QDateTime::fromMSecsSinceEpoch(e.resolvedAtMs)
                  .toString(QStringLiteral("MM-dd HH:mm:ss"))
            : QStringLiteral("—");
        tableEvents_->setItem(row, EvResolved,
                              new QTableWidgetItem(resolvedText));
    }
}

int AlarmScreen::currentRuleId() const {
    const int row = tableRules_->currentRow();
    if (row < 0) return -1;
    QTableWidgetItem* item = tableRules_->item(row, RuleId);
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

int AlarmScreen::currentEventId() const {
    const int row = tableEvents_->currentRow();
    if (row < 0) return -1;
    QTableWidgetItem* item = tableEvents_->item(row, EvTime);
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

void AlarmScreen::onAddRule() {
    const QString sensorId = editSensorId_->text().trimmed();
    if (sensorId.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"),
            QStringLiteral("请填写机柜 ID"));
        return;
    }

    bool ok = false;
    const double threshold = editThreshold_->text().toDouble(&ok);
    if (!ok) {
        QMessageBox::information(this, QStringLiteral("提示"),
            QStringLiteral("阈值必须是数字"));
        return;
    }

    const QString type = comboType_->currentData().toString();
    const auto direction = static_cast<AlarmDirection>(
        comboDirection_->currentData().toInt());

    if (!service_.addRule(sensorId, type, threshold, direction)) {
        QMessageBox::warning(this, QStringLiteral("新增失败"),
            QStringLiteral("规则未能写入数据库"));
        return;
    }
    // 成功后 service 会发 alarmChanged，表格自动刷新，这里无需手动 reload
}

void AlarmScreen::onRemoveRule() {
    const int ruleId = currentRuleId();
    if (ruleId < 0) {
        QMessageBox::information(this, QStringLiteral("提示"),
            QStringLiteral("请先在规则列表中选中一行"));
        return;
    }
    if (!service_.removeRule(ruleId)) {
        QMessageBox::warning(this, QStringLiteral("删除失败"),
            QStringLiteral("规则未能从数据库中删除"));
    }
}

void AlarmScreen::onToggleRule() {
    const int ruleId = currentRuleId();
    if (ruleId < 0) {
        QMessageBox::information(this, QStringLiteral("提示"),
            QStringLiteral("请先在规则列表中选中一行"));
        return;
    }

    // 取当前行显示的是"启用"还是"停用"，点一下就反过来
    // 状态就写在表格里，不用再查一次库
    const int row = tableRules_->currentRow();
    const bool currentlyEnabled =
        tableRules_->item(row, RuleEnabled)->text() == QStringLiteral("启用");
    service_.setRuleEnabled(ruleId, !currentlyEnabled);
}

void AlarmScreen::onAcknowledge() {
    const int eventId = currentEventId();
    if (eventId < 0) {
        QMessageBox::information(this, QStringLiteral("提示"),
            QStringLiteral("请先在事件列表中选中一行"));
        return;
    }
    if (!service_.acknowledge(eventId)) {
        // 常见原因：这条已经点过确认了，或者数值已经自动恢复正常
        QMessageBox::information(this, QStringLiteral("无需确认"),
            QStringLiteral("该告警已确认或已恢复"));
    }
}

void AlarmScreen::onAlarmChanged() {
    reloadRulesTable();
    reloadEventsTable();
}

void AlarmScreen::onAlarmTriggered(const AlarmEvent& event) {
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(event.triggeredAtMs);
    lblBanner_->setText(QStringLiteral(
        "⚠ %1  新告警：%2 的%3 达到 %4 %5")
        .arg(dt.toString(QStringLiteral("HH:mm:ss")),
             event.sensorId,
             typeToDisplay(event.type),
             QString::number(event.value, 'f', 2),
             unitForType(event.type)));
    lblBanner_->setStyleSheet(QStringLiteral(
        "padding: 6px; border: 1px solid #c0392b;"
        " background: #fdecea; color: #c0392b; font-weight: bold;"));
}

void AlarmScreen::onAlarmResolved(const AlarmEvent& event) {
    lblBanner_->setText(QStringLiteral("✓ %1 的%2 已恢复正常")
        .arg(event.sensorId, typeToDisplay(event.type)));
    lblBanner_->setStyleSheet(QStringLiteral(
        "padding: 6px; border: 1px solid #27ae60;"
        " background: #eafaf1; color: #27ae60;"));
}

} // namespace sensorhub