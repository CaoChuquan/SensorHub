#pragma once

#include "data/SensorTypes.h"
#include <QWidget>
#include <QHash>
#include <QString>

class QTableWidget;
class QLabel;
class QTimer;

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

    // 每秒检查有没有收到新读数，超过五秒没有新读数在表格上方挂一条黄色提示
    void updateStaleBanner();
private:
    // 根据指标类型返回表格列号
    static int columnForType(const QString& type);

    QHash<QString, int> rack_row_;   // 机柜 ID 到表格行号的映射

    QTableWidget* table_ = nullptr;
    QLabel* staleLabel_ = nullptr;   // 数据过期提示条，正常时隐藏
    QTimer* staleTimer_ = nullptr;   // 每秒检查一次数据是否最新

    // 最后一次收到读数的本地时刻（毫秒），0 表示一条都还没收到
    qint64 lastReadingMs_ = 0;
};

} // namespace sensorhub