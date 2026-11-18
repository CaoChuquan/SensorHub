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

    // 在状态栏显示“数据从哪来”
    void setDataSourceLabel(const QString& lable);
    // 在状态栏显示采集链路是否正常
    void setLinkState(bool online, const QString& reason);    
private:
    QTabWidget* tabs_ = nullptr;       // 多页容器，每个功能一个 tab
    QLabel* sourceLabel_ = nullptr;    // 右下角状态栏，显示数据源
    QLabel* linkLabel_ = nullptr;      // 状态栏的"采集状态"标签
};

} // namespace sensorhub