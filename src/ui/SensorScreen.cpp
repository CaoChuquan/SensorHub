#include "SensorScreen.h"
#include "core/DataHub.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QDateTime>

namespace sensorhub {

namespace {
    
// 表格列定义
constexpr int kColRack = 0;      // 机柜 ID
constexpr int kColTemp = 1;      // 温度
constexpr int kColHumidity = 2;  // 湿度
constexpr int kColVoltage = 3;   // 市电电压
constexpr int kColTime = 4;      // 最近一次更新时间
constexpr int kColumnCount = 5;

// 暂时固定的机柜 id
const char* kRackIds[3] = { "rack1", "rack2", "rack3" };

} // namespace

SensorScreen::SensorScreen(DataHub& hub, QWidget* parent)
    : QWidget(parent)
{
    // 建表格
    table_ = new QTableWidget(3, kColumnCount, this);
    table_->setHorizontalHeaderLabels({

    });
    // 对表格进行设置
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // 初始化 3 个固定行：写好机柜名，数据列先放占位符 "—"。
    for (int row = 0; row < 3; ++row) {
        const QString rackId = QString::fromLatin1(kRackIds[row]);
        rack_row_.insert(rackId, row);  // 记下 ID->行号，读数到来时直接查

        table_->setItem(row, kColRack, new QTableWidgetItem(rackId));
        for (int col = kColTemp; col <= kColTime; ++col) {
            table_->setItem(row, col, new QTableWidgetItem(QStringLiteral("—")));
        }
    }

    // SensorScreen 析构时 Qt 自动删除 table_
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(table_);

    // 连接 DataHub
    connect(&hub, &DataHub::readingReceived, 
            this, &SensorScreen::onReading);
}

int SensorScreen::columnForType(const QString& type) {
    if (type == QLatin1String("temperature")) return kColTemp;
    if (type == QLatin1String("humidity")) return kColHumidity;
    if (type == QLatin1String("voltage")) return kColVoltage;
    return -1; // 未认识的指标类型：忽略，不显示
}

void SensorScreen::onReading(const SensorReading& reading) {
    // 定位行：不是表格里的机柜就忽略
    const auto rowIt = rack_row_.constFind(reading.sensorId);
    if (rowIt == rack_row_.constEnd()) {
        return;
    }
    // 定位列：不认识的指标类型也忽略
    const int col = columnForType(reading.type);
    if (col < 0) {
        return;
    }

    const int row = rowIt.value();


    table_->item(row, col)->setText(QString::number(reading.value, 'f', 2));
    // 无论哪种指标更新，都刷新该行的"更新时间"
    table_->item(row, kColTime)->setText(
        QDateTime::fromMSecsSinceEpoch(reading.timestampMs)
            .toString(QStringLiteral("hh:mm:ss")));
}

} // namespace sensorhub