#pragma once

#include "data/SensorTypes.h"
#include <QWidget>
#include <QHash>
#include <QString>

class QTableWidget;

namespace sensorhub {

class DataHub;

// 实时读数界面类
class SensorScreen : public QWidget {
    Q_OBJECT
public:
    explicit SensorScreen(DataHub& hub, QWidget* parent = nullptr);
private slots:
    // 槽函数：hub 每广播一条读数就被调用一次
    void onReading(const SensorReading& reading);
private:
    // 根据指标类型返回表格列号
    static int columnForType(const QString& type);

    QTableWidget* table_ = nullptr;
    QHash<QString, int> rack_row_;   // 机柜 ID 到表格行号的映射
};

} // namespace sensorhub