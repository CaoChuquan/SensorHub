#include "MainWindow.h"
#include "SensorScreen.h"
#include "core/DataHub.h"
#include "WatchlistScreen.h"
#include "ui/AnalysisScreen.h"
#include "ui/ChartScreen.h"
#include <QTabWidget>
#include <QStringLiteral>

namespace sensorhub {

MainWindow::MainWindow(DataHub& hub, WatchlistService& wl_service,
                       AnalysisService& al_service,
                       ChartService& chartService,
                       QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("机房环境监控终端"));
    resize(900, 600);

    // 传感器页面
    tabs_ = new QTabWidget(this);
    tabs_->addTab(new SensorScreen(hub, tabs_),
                  QStringLiteral("实时读数"));
    // 关注列表页面
    tabs_->addTab(new WatchlistScreen(hub, wl_service, tabs_),
                  QStringLiteral("关注列表"));

    // 统计分析页面
    tabs_->addTab(new AnalysisScreen(al_service, tabs_), QStringLiteral("统计分析"));

    // 时序图页面
    tabs_->addTab(new ChartScreen(chartService, tabs_), QStringLiteral("时序图"));

    setCentralWidget(tabs_);
}

} // namespace sensorhub