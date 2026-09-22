#pragma once

#include <QMainWindow>

class QTabWidget;

namespace sensorhub {

class DataHub;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(DataHub& hub, QWidget* parent = nullptr);
private:
    QTabWidget* tabs_ = nullptr;
};

} // namespace sensorhub