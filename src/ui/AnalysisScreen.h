#pragma once

#include <QWidget>
#include "services/AnalysisService.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace sensorhub {

// 统计分析界面类
class AnalysisScreen : public QWidget {
    Q_OBJECT
public:
    // service: 统计分析服务
    explicit AnalysisScreen(AnalysisService& service, QWidget* parent = nullptr);
private slots:
    void onAnalyzeClicked();  // 点"分析"：收集输入，调 service
    void onAnalysisComplete(const QString& sensorId, const QString& type,
                            const AnalysisStats& stats,
                            const QVector<AnomalyPoint>& anomalies,
                            const QString& error);
private:
    void setupUi();

    QLineEdit*   editSensor_ = nullptr;  // 机柜 ID
    QComboBox*   comboType_  = nullptr;  // temperature / humidity / voltage
    QLineEdit*   editCount_  = nullptr;  // 样本数 N
    QPushButton* btnAnalyze_ = nullptr;

    QLabel* lblMean_ = nullptr;
    QLabel* lblStd_ = nullptr;
    QLabel* lblMin_ = nullptr;
    QLabel* lblMax_ = nullptr;
    QLabel* lblCount_ = nullptr;

    QTableWidget* tableAnomaly_ = nullptr;

    AnalysisService& service_;
};

} // namespace sensorhub