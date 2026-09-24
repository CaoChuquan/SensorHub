#pragma once

#include <QMainWindow>

class QTabWidget;

namespace sensorhub {

class DataHub;          // 读数来源
class WatchlistService; // 关注列表服务

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(DataHub& hub, WatchlistService& wl_service,
                        QWidget* parent = nullptr);
private:
    QTabWidget* tabs_ = nullptr;
};

} // namespace sensorhub