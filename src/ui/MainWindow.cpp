#include "MainWindow.h"
#include "SensorScreen.h"
#include "core/DataHub.h"
#include "WatchlistScreen.h"
#include <QTabWidget>
#include <QStringLiteral>

namespace sensorhub {

MainWindow::MainWindow(DataHub& hub, WatchlistService& wl_service, 
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

    setCentralWidget(tabs_);
}

} // namespace sensorhub