#include "ui/ChartScreen.h"
#include "services/ChartService.h"

#include <limits>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPen>
#include <QPushButton>
#include <QVBoxLayout>



// Qt5 下 Charts 类位于 QtCharts 命名空间，用官方宏引入全局名
// Qt6 起类已在全局命名空间，什么都不用做（Qt 6.11 已删除该宏）
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
QT_CHARTS_USE_NAMESPACE
#endif

namespace sensorhub {

namespace {
// 指标类型 → Y 轴单位（温度/湿度/电压，本项目固定三种）
QString unitForType(const QString& type) {
    if (type == QLatin1String("temperature")) return QStringLiteral("°C");
    if (type == QLatin1String("humidity"))    return QStringLiteral("%RH");
    if (type == QLatin1String("voltage"))    return QStringLiteral("V");
    return {};
}
} // namespace

ChartScreen::ChartScreen(ChartService& service, QWidget* parent)
    : QWidget(parent), service_(service) {
    setupUi();
}

void ChartScreen::setupUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // 控制栏
    auto* controls = new QHBoxLayout();
    controls->addWidget(new QLabel(QStringLiteral("指标:"), this));
    comboType_ = new QComboBox(this);
    // 显示中文、内部数据用英文 type（与库里存的取值一致）
    comboType_->addItem(QStringLiteral("温度"), QStringLiteral("temperature"));
    comboType_->addItem(QStringLiteral("湿度"), QStringLiteral("humidity"));
    comboType_->addItem(QStringLiteral("电压"), QStringLiteral("voltage"));
    controls->addWidget(comboType_);

    controls->addSpacing(12);
    controls->addWidget(new QLabel(QStringLiteral("样本数/机柜:"), this));
    editCount_ = new QLineEdit(QStringLiteral("60"), this);
    editCount_->setMaximumWidth(60);
    editCount_->setValidator(new QIntValidator(1, 10000, this));
    controls->addWidget(editCount_);

    controls->addSpacing(12);
    btnRefresh_ = new QPushButton(QStringLiteral("刷新图表"), this);
    btnRefresh_->setStyleSheet(QStringLiteral("padding: 4px 16px;"));
    controls->addWidget(btnRefresh_);
    controls->addStretch();
    layout->addLayout(controls);

    // 切换指标等同于一次刷新：少一次手动点击，也避免停留在旧指标的图上
    connect(comboType_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { redraw(); });
    connect(btnRefresh_, &QPushButton::clicked,
            this, &ChartScreen::onRefreshClicked);

    // 图表区
    chart_ = new QChart();
    chart_->setTitle(QStringLiteral("点击「刷新图表」加载数据"));
    chart_->legend()->setVisible(true);
    chart_->legend()->setAlignment(Qt::AlignBottom);

    chartView_ = new QChartView(chart_, this);
    chartView_->setRenderHint(QPainter::Antialiasing);  // 折线抗锯齿
    layout->addWidget(chartView_, 1);
}

void ChartScreen::onRefreshClicked() {
    redraw();
}

void ChartScreen::redraw() {
    // 1. 拆光旧图：移除全部曲线和轴，下一轮重新 new
    chart_->removeAllSeries();
    const auto oldAxes = chart_->axes();
    for (auto* axis : oldAxes) {
        chart_->removeAxis(axis);
    }

    const QString type = comboType_->currentData().toString();
    int sampleCount = editCount_->text().toInt();
    if (sampleCount <= 0) sampleCount = 60;  // 兜底（QIntValidator 已挡住非法输入）

    // 2. 取数：关注列表中该指标的机柜，每个一条曲线
    const QVector<ChartSeriesData> seriesList =
        service_.load(type, sampleCount);
    if (seriesList.isEmpty()) {
        chart_->setTitle(QStringLiteral(
            "暂无可绘制的数据：请先在「关注列表」添加 %1 指标，"
            "并等待历史存档积累").arg(comboType_->currentText()));
        return;
    }

    // 3. 建一对共享轴。QDateTimeAxis 的 x 直接吃毫秒时间戳。
    auto* axisX = new QDateTimeAxis();
    axisX->setFormat(QStringLiteral("HH:mm:ss"));
    axisX->setTitleText(QStringLiteral("时间"));
    chart_->addAxis(axisX, Qt::AlignBottom);

    auto* axisY = new QValueAxis();
    axisY->setTitleText(unitForType(type));
    axisY->setLabelFormat(QStringLiteral("%.2f"));
    chart_->addAxis(axisY, Qt::AlignLeft);

    // 4. 边遍历边求全局范围（时间 + 数值），最后一次性定轴
    qint64 minTs = std::numeric_limits<qint64>::max();
    qint64 maxTs = std::numeric_limits<qint64>::min();
    double minVal = std::numeric_limits<double>::max();
    double maxVal = std::numeric_limits<double>::lowest();

    // 多机柜配色：经典 Tableau 4 色，色差大，投影到灰度屏也可分辨
    static const QStringList kColors = {
        QStringLiteral("#1f77b4"), QStringLiteral("#ff7f0e"),
        QStringLiteral("#2ca02c"), QStringLiteral("#d62728"),
    };

    for (int i = 0; i < seriesList.size(); ++i) {
        const auto& data = seriesList[i];

        auto* line = new QLineSeries();
        line->setName(data.name);
        const QColor color(kColors[i % kColors.size()]);
        QPen pen(color);
        pen.setWidth(2);
        line->setPen(pen);

        line->append(data.points);  // 点已按时间正序
        for (const auto& p : data.points) {
            minTs  = std::min(minTs,  static_cast<qint64>(p.x()));
            maxTs  = std::max(maxTs,  static_cast<qint64>(p.x()));
            minVal = std::min(minVal, p.y());
            maxVal = std::max(maxVal, p.y());
        }

        chart_->addSeries(line);
        line->attachAxis(axisX);
        line->attachAxis(axisY);
    }

    // 5. 定轴范围。Y 轴上下各留 10% 边距；全平数据（max==min）时给固定跨度，
    //    否则轴会退化成长度为 0 的区间，画面是一条贴边直线。
    axisX->setRange(QDateTime::fromMSecsSinceEpoch(minTs),
                    QDateTime::fromMSecsSinceEpoch(maxTs));
    double span = maxVal - minVal;
    if (span < 1e-6) span = 1.0;
    axisY->setRange(minVal - span * 0.1, maxVal + span * 0.1);

    chart_->setTitle(QStringLiteral("%1 时序对比（%2 条曲线）")
                         .arg(comboType_->currentText())
                         .arg(seriesList.size()));
}

} // namespace sensorhub