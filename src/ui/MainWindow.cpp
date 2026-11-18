#include "MainWindow.h"
#include "SensorScreen.h"
#include "core/DataHub.h"
#include "WatchlistScreen.h"
#include "ui/AnalysisScreen.h"
#include "ui/ChartScreen.h"
#include "ui/AlarmScreen.h"
#include <QTabWidget>
#include <QStringLiteral>
#include <QStatusBar>

namespace sensorhub {

MainWindow::MainWindow(DataHub& hub, WatchlistService& wl_service,
                       AnalysisService& al_service,
                       ChartService& chartService,
                       AlarmService& alarmService,
                       QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("机房环境监控终端"));
    resize(900, 600);

    // 传感器页面
    tabs_ = new QTabWidget(this);
    tabs_->addTab(new SensorScreen(hub, tabs_), QStringLiteral("实时读数"));
    // 关注列表页面
    tabs_->addTab(new WatchlistScreen(hub, wl_service, tabs_), QStringLiteral("关注列表"));
    // 统计分析页面
    tabs_->addTab(new AnalysisScreen(al_service, tabs_), QStringLiteral("统计分析"));
    // 时序图页面
    tabs_->addTab(new ChartScreen(chartService, tabs_), QStringLiteral("时序图"));
    // 告警页面
    tabs_->addTab(new AlarmScreen(alarmService, tabs_), QStringLiteral("告警面板")); 
    setCentralWidget(tabs_);

    // 状态栏：显示："采集：正常/中断" 
    linkLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(linkLabel_);
    // 状态栏：显示数据来源
    sourceLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(sourceLabel_);
}

void MainWindow::setDataSourceLabel(const QString& label) {
    sourceLabel_->setText(QStringLiteral("数据源：") + label);
}

void MainWindow::setLinkState(bool online, const QString& reason) {
    if (online) {
        linkLabel_->setText(QStringLiteral("采集：正常"));
        linkLabel_->setStyleSheet(QStringLiteral("color:#2e7d32;"));
    } 
    else {
        linkLabel_->setText(QStringLiteral("采集：中断（%1）").arg(reason));
        linkLabel_->setStyleSheet(QStringLiteral("color:#c62828; font-weight:bold;"));
    }
}

} // namespace sensorhub