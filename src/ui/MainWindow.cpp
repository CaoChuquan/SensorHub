#include "MainWindow.h"
#include "SensorScreen.h"
#include "core/DataHub.h"
#include <QTabWidget>
#include <QStringLiteral>

namespace sensorhub {

MainWindow::MainWindow(DataHub& hub, QWidget* parent)
    : QMainWindow(parent) 
{
    setWindowTitle(QStringLiteral("机房环境监控终端"));
    resize(900, 600);

    tabs_ = new QTabWidget(this);
    tabs_->addTab(new SensorScreen(hub, tabs_),
                  QStringLiteral("实时读数"));

    setCentralWidget(tabs_);
}

} // namespace sensorhub