#include "ui/AnalysisScreen.h"
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace sensorhub {

namespace {
// 指标类型 → 显示单位
QString unitForType(const QString& type) {
    if (type == QLatin1String("temperature")) return QStringLiteral("°C");
    if (type == QLatin1String("humidity"))    return QStringLiteral("%RH");
    if (type == QLatin1String("voltage"))    return QStringLiteral("V");
    return {};
}
} // namespace

AnalysisScreen::AnalysisScreen(AnalysisService& service, QWidget* parent)
    : QWidget(parent), service_(service) {
    setupUi();

    connect(&service_, &AnalysisService::analysisComplete,
            this, &AnalysisScreen::onAnalysisComplete);
}

void AnalysisScreen::setupUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // 查询行
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(QStringLiteral("机柜 ID:"), this));
    editSensor_ = new QLineEdit(QStringLiteral("rack1"), this);
    editSensor_->setMinimumWidth(120);
    row->addWidget(editSensor_);

    row->addSpacing(12);
    row->addWidget(new QLabel(QStringLiteral("指标:"), this));
    // 下拉而不是自由输入：类型只有三种，从源头杜绝拼写错误导致查不到数据
    comboType_ = new QComboBox(this);
    comboType_->addItem(QStringLiteral("温度 (temperature)"),
                        QStringLiteral("temperature"));
    comboType_->addItem(QStringLiteral("湿度 (humidity)"),
                        QStringLiteral("humidity"));
    comboType_->addItem(QStringLiteral("电压 (voltage)"),
                        QStringLiteral("voltage"));
    row->addWidget(comboType_);

    row->addSpacing(12);
    row->addWidget(new QLabel(QStringLiteral("样本数:"), this));
    editCount_ = new QLineEdit(QStringLiteral("60"), this);
    editCount_->setMaximumWidth(60);
    // 只允许填 1~10000 的正整数，非法字符直接敲不进去
    editCount_->setValidator(new QIntValidator(1, 10000, this));
    row->addWidget(editCount_);

    row->addSpacing(12);
    btnAnalyze_ = new QPushButton(QStringLiteral("分析"), this);
    btnAnalyze_->setStyleSheet(QStringLiteral("padding: 4px 16px;"));
    row->addWidget(btnAnalyze_);
    row->addStretch();
    layout->addLayout(row);

    connect(btnAnalyze_, &QPushButton::clicked,
            this, &AnalysisScreen::onAnalyzeClicked);

    // ── 中央：左统计卡 + 右异常表 ────────────────────────────────────────────
    auto* center = new QHBoxLayout();

    // 左：描述性统计（QFormLayout：标签 + 值 两列对齐）
    auto* statsBox = new QGroupBox(QStringLiteral("统计"), this);
    auto* form = new QFormLayout(statsBox);
    lblMean_  = new QLabel(QStringLiteral("—"), statsBox);
    lblStd_   = new QLabel(QStringLiteral("—"), statsBox);
    lblMin_   = new QLabel(QStringLiteral("—"), statsBox);
    lblMax_   = new QLabel(QStringLiteral("—"), statsBox);
    lblCount_ = new QLabel(QStringLiteral("—"), statsBox);
    form->addRow(QStringLiteral("均值:"),    lblMean_);
    form->addRow(QStringLiteral("标准差:"), lblStd_);
    form->addRow(QStringLiteral("最小值:"), lblMin_);
    form->addRow(QStringLiteral("最大值:"), lblMax_);
    form->addRow(QStringLiteral("样本数:"), lblCount_);
    statsBox->setMinimumWidth(240);
    center->addWidget(statsBox);

    // 右：离群点表格
    auto* anomalyBox = new QGroupBox(
        QStringLiteral("异常点（|z-score| > 3）"), this);
    auto* anomalyLayout = new QVBoxLayout(anomalyBox);
    tableAnomaly_ = new QTableWidget(0, 3, anomalyBox);
    tableAnomaly_->setHorizontalHeaderLabels({
        QStringLiteral("时间"),
        QStringLiteral("数值"),
        QStringLiteral("z-score"),
    });
    tableAnomaly_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableAnomaly_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableAnomaly_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tableAnomaly_->verticalHeader()->setVisible(false);  // 行号对用户无意义
    anomalyLayout->addWidget(tableAnomaly_);
    center->addWidget(anomalyBox, 1);

    layout->addLayout(center, 1);
}

void AnalysisScreen::onAnalyzeClicked() {
    const QString sensorId = editSensor_->text().trimmed();
    const QString type = comboType_->currentData().toString();
    const int count = editCount_->text().toInt();
    if (sensorId.isEmpty() || count <= 0) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请输入有效的机柜 ID 和样本数"));
        return;
    }

    // 进入 loading 态：防止重复点击
    btnAnalyze_->setEnabled(false);
    btnAnalyze_->setText(QStringLiteral("分析中…"));

    service_.analyzeSensor(sensorId, type, count);
}

void AnalysisScreen::onAnalysisComplete(const QString& sensorId,
                                        const QString& type,
                                        const AnalysisStats& stats,
                                        const QVector<AnomalyPoint>& anomalies,
                                        const QString& error) {
    Q_UNUSED(sensorId)

    // 无论成败先恢复按钮
    btnAnalyze_->setEnabled(true);
    btnAnalyze_->setText(QStringLiteral("分析"));

    if (!error.isEmpty()) {
        // 查不到历史数据是最常见的"失败"（程序刚启动、还没攒够存档），
        // 用提示框而不是在标签里显示错误，更醒目
        QMessageBox::information(this, QStringLiteral("无法分析"), error);
        return;
    }

    // 统计量带单位显示（均值/极值与原始读数同量纲；标准差也是）
    const QString unit = unitForType(type);
    auto fmt = [&unit](double v, int prec) {
        const QString num = QString::number(v, 'f', prec);
        return unit.isEmpty() ? num : QStringLiteral("%1 %2").arg(num, unit);
    };
    lblMean_->setText(fmt(stats.mean, 2));
    lblStd_->setText(fmt(stats.std, 2));
    lblMin_->setText(fmt(stats.min, 2));
    lblMax_->setText(fmt(stats.max, 2));
    lblCount_->setText(QString::number(stats.count));

    // 异常点填表格（samples 在仓储里已翻成时间正序，z 挑出来后天然有序）
    tableAnomaly_->setRowCount(anomalies.size());
    for (int row = 0; row < anomalies.size(); ++row) {
        const auto& a = anomalies[row];
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(a.timestampMs);
        tableAnomaly_->setItem(row, 0,
                               new QTableWidgetItem(dt.toString(QStringLiteral("MM-dd HH:mm:ss"))));
        tableAnomaly_->setItem(row, 1,
                               new QTableWidgetItem(fmt(a.value, 2)));
        // z-score 保留正负号：正的表示偏高，负的表示偏低，运维排查方向不同
        tableAnomaly_->setItem(row, 2,
                               new QTableWidgetItem(QString::number(a.zscore, 'f', 2)));
    }
}

} // namespace sensorhub