#pragma once

#include <QMainWindow>

class QTabWidget;
class QLabel;

namespace sensorhub {

class DataHub;           // 读数来源
class WatchlistService;  // 关注列表服务
class AnalysisService;   // 统计分析服务
class ChartService;      // 时序图服务
class AlarmService;      // 告警服务

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(DataHub& hub, WatchlistService& wl_service,
                        AnalysisService& al_service,
                        ChartService& chartService,
                        AlarmService& alarmService,
                        QWidget* parent = nullptr);

    void setDataSourceLabel(const QString& lable);
private:
    QTabWidget* tabs_ = nullptr;       // 多页容器，每个功能一个 tab
    QLabel* sourceLabel_ = nullptr;    // 右下角状态栏，显示数据源
};

} // namespace sensorhub