#include "WatchlistScreen.h"
#include "core/DataHub.h"
#include "services/WatchlistService.h"
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace sensorhub {

namespace {

// 指标类型 → 显示单位，用于格式化"最新读数"列
QString unitForType(const QString& type) {
    if (type == QLatin1String("temperature")) return QStringLiteral("°C");
    if (type == QLatin1String("humidity")) return QStringLiteral("%RH");
    if (type == QLatin1String("voltage")) return QStringLiteral("V");
    return {};
}

// 拼接 key：sensorId + "|" + type → 唯一标识一条关注
QString makeKey(const QString& sensorId, const QString& type) {
    return sensorId + QLatin1Char('|') + type;
}

} // namespace

WatchlistScreen::WatchlistScreen(DataHub& hub, WatchlistService& service,
    QWidget* parent) : QWidget(parent), hub_(hub), service_(service)
{
    setupUi();
    reloadFromDb(); // 加载已有关注列表

    // 订阅 DataHub：每条读数到达时检查是否匹配某条关注
    connect(&hub_, &DataHub::readingReceived,
            this, &WatchlistScreen::onReading);
    // 订阅 Service 的变更信号：add/remove 后自动刷新表格
    connect(&service_, &WatchlistService::watchlistChanged,
            this, &WatchlistScreen::reloadFromDb);
}

void WatchlistScreen::setupUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // 工具栏
    auto* toolbar = new QHBoxLayout();
    btnAdd_ = new QPushButton(QStringLiteral("添加关注"), this);
    btnRemove_ = new QPushButton(QStringLiteral("删除选中"), this);
    btnRefresh_ = new QPushButton(QStringLiteral("刷新"), this);
    for (auto* btn : { btnAdd_, btnRemove_, btnRefresh_ }) {
        btn->setStyleSheet(QStringLiteral("padding: 4px 12px;"));
        toolbar->addWidget(btn);
    }
    toolbar->addStretch();
    layout->addLayout(toolbar);

    connect(btnAdd_, &QPushButton::clicked, this, &WatchlistScreen::onAddClicked);
    connect(btnRemove_, &QPushButton::clicked, this, &WatchlistScreen::onRemoveClicked);
    connect(btnRefresh_, &QPushButton::clicked, this, &WatchlistScreen::reloadFromDb);

    // 表格
    table_ = new QTableWidget(this);
    table_->setColumnCount(ColCount);
    table_->setHorizontalHeaderLabels({
        QStringLiteral("ID"),
        QStringLiteral("传感器"),
        QStringLiteral("类型"),
        QStringLiteral("标签"),
        QStringLiteral("添加时间"),
        QStringLiteral("最新读数")
    });
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setColumnWidth(ColId, 40);
    table_->setColumnWidth(ColSensor, 90);
    table_->setColumnWidth(ColType, 80);

    layout->addWidget(table_, 1);  // stretch=1：表格占满剩余空间
}

void WatchlistScreen::reloadFromDb() {
    // 从 Service 加载全部关注条目（Service 内部委托给 Repository）
    entry_ = service_.loadAll();

    // 清空旧映射，按新数据重建
    keyToRow_.clear();
    table_->setRowCount(static_cast<int>(entry_.size()));

    for (int row = 0; row < entry_.size(); ++row) {
        const auto& e = entry_[row];

        table_->setItem(row, ColId,
            new QTableWidgetItem(QString::number(e.id)));
        table_->setItem(row, ColSensor,
            new QTableWidgetItem(e.sensorId));
        table_->setItem(row, ColType,
            new QTableWidgetItem(e.type));
        table_->setItem(row, ColLabel,
            new QTableWidgetItem(e.label.isEmpty()
                ? QStringLiteral("—") : e.label));

        // 添加时间格式化到分钟精度
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(e.addedAtMs);
        table_->setItem(row, ColAddedAt,
            new QTableWidgetItem(dt.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));

        // 最新读数先占位，等 DataHub 推数据来刷新
        table_->setItem(row, ColLatest,
            new QTableWidgetItem(QStringLiteral("—")));

        // 建立 key→行号 映射：onReading 时用它 O(1) 定位
        keyToRow_.insert(makeKey(e.sensorId, e.type), row);
    }
}

void WatchlistScreen::onAddClicked() {
    // 用 QInputDialog 依次收集 sensorId / type / label
// 简单但够用；将来可以换成自定义对话框（下拉选择已有传感器列表）
    bool ok = false;
    const QString sensorId = QInputDialog::getText(this,
        QStringLiteral("添加关注"),
        QStringLiteral("传感器 ID (如 rack1):"),
        QLineEdit::Normal, QStringLiteral("rack1"), &ok);
    if (!ok || sensorId.isEmpty()) return;

    const QString type = QInputDialog::getText(this,
        QStringLiteral("添加关注"),
        QStringLiteral("指标类型 (temperature / humidity / voltage):"),
        QLineEdit::Normal, QStringLiteral("temperature"), &ok);
    if (!ok || type.isEmpty()) return;

    const QString label = QInputDialog::getText(this,
        QStringLiteral("添加关注"),
        QStringLiteral("标签 (可空):"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok) return;

    // 调用 Service 持久化。成功后 Service 会发射 watchlistChanged，
    // 连接到 reloadFromDb，表格自动刷新——无需手动调 reload。
    if (!service_.add(sensorId, type, label)) {
        QMessageBox::warning(this, QStringLiteral("失败"),
            QStringLiteral("添加失败，请检查输入或数据库状态"));
    }
}

void WatchlistScreen::onReading(const SensorReading& reading) {
    // 拼出 key，查映射表：不是关注的条目就直接返回
    const QString key = makeKey(reading.sensorId, reading.type);
    const auto it = keyToRow_.constFind(key);
    if (it == keyToRow_.constEnd()) return;

    const int row = it.value();
    if (row < 0 || row >= table_->rowCount()) return;

    // 格式化读数：数值 + 单位（温度 24.50 °C / 湿度 50.30 %RH / 电压 220.00 V）
    const QString unit = unitForType(reading.type);
    const QString text = unit.isEmpty()
        ? QString::number(reading.value, 'f', 2)
        : QStringLiteral("%1 %2").arg(reading.value, 0, 'f', 2).arg(unit);

    // 只更新已存在 item 的文本，不 new 新 item——与 SensorScreen 同策略
    if (auto* item = table_->item(row, ColLatest)) {
        item->setText(text);
    }
}

void WatchlistScreen::onRemoveClicked() {
    const int row = table_->currentRow();
    if (row < 0 || row >= entry_.size()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择一行再删除"));
        return;
    }

    const int id = entry_[row].id;
    // removeById 成功后 Service 发射 watchlistChanged → reloadFromDb 自动刷新
    if (!service_.removeById(id)) {
        QMessageBox::warning(this, QStringLiteral("失败"),
                             QStringLiteral("删除失败"));
    }
}

} // namespace sensorhub