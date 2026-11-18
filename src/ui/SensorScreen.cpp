#include "SensorScreen.h"
#include "core/DataHub.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QDateTime>
#include <QTimer>
#include <QLabel>

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
        QStringLiteral("机柜"),
        QStringLiteral("温度(°C)"),
        QStringLiteral("湿度(%RH)"),
        QStringLiteral("电压(V)"),
        QStringLiteral("更新时间"),
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

    // 数据过期提示条
    // 表格只保留“最新值”，设备一旦掉线，数字就会停在最后一次的值上，
    // 从界面上看不出它已经过期，这条提示专门补上这个信息
    staleLabel_ = new QLabel(this);
    staleLabel_->setWordWrap(true);
    staleLabel_->setStyleSheet(QStringLiteral(
        "background:#fff3cd; color:#8a6d3b;"
        "border:1px solid #ffeeba; padding:6px;"));
    staleLabel_->hide();   // 数据正常时不显示
    layout->addWidget(staleLabel_);

    layout->addWidget(table_);

    staleTimer_ = new QTimer(this);
    staleTimer_->start(1000);
    connect(staleTimer_, &QTimer::timeout,
            this, &SensorScreen::updateStaleBanner);

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

    // 收到任何读数都说明采集链路还活着：刷新时间戳，撤掉过期提示
    lastReadingMs_ = QDateTime::currentMSecsSinceEpoch();
    if (!staleLabel_->isHidden()) {
        staleLabel_->hide();
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

void SensorScreen::updateStaleBanner() {
    // 阈值：3 秒。正常采集是每秒一批，超过 3 秒没数据基本确定连接断开
    // 留一点余量是为了容忍偶发的调度抖动，不至于闪一下提示
    constexpr qint64 kStaleMs = 3000;

    if (lastReadingMs_ == 0) {
        // 程序刚启动、还没收到第一条数据
        staleLabel_->setText(QStringLiteral("等待采集数据…"));
        staleLabel_->show();
        return;
    }

    const qint64 silentMs = QDateTime::currentMSecsSinceEpoch() - lastReadingMs_;
    if (silentMs > kStaleMs) {
        staleLabel_->setText(
            QStringLiteral("采集中断：已有 %1 秒没有新数据，表中的数值是"
                           "最后一次收到的结果，不代表当前状态。")
                .arg(silentMs / 1000));
        staleLabel_->show();
    } 
    else {
        staleLabel_->hide();
    }
}

} // namespace sensorhub