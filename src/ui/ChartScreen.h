#pragma once

#include <QWidget>

class QComboBox;
class QLineEdit;
class QPushButton;

// Charts 类的命名空间在 Qt 大版本间有差异：
//   Qt5  —— 位于 QtCharts 命名空间，再用 using 引入全局名
//   Qt6  —— 已移入全局命名空间（Qt 6.11 连 QT_CHARTS_USE_NAMESPACE
//           兼容宏都移除了），直接前向声明即可
// QWidget 已间接包含 <QtGlobal>，这里用 QT_VERSION 做版本判断。
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
class QChart;
class QChartView;
#else
namespace QtCharts {
class QChart;
class QChartView;
}
using QtCharts::QChart;
using QtCharts::QChartView;
#endif

namespace sensorhub {

class ChartService;

// 时序图界面类
class ChartScreen : public QWidget {
    Q_OBJECT
public:
    explicit ChartScreen(ChartService& service, QWidget* parent = nullptr);

private slots:
    void onRefreshClicked();  // 点“刷新”（或切换指标）：重新取数并重绘

private:
    void setupUi();
    void redraw();  // 清旧图 → 取数 → 逐条 QLineSeries → 定轴范围

    // UI 元素
    QComboBox* comboType_ = nullptr;  // 指标选择
    QLineEdit* editCount_ = nullptr;  // 每条曲线取最近 N 个点
    QPushButton* btnRefresh_ = nullptr;

    QChart* chart_ = nullptr;
    QChartView* chartView_ = nullptr;

    // 依赖
    ChartService& service_;
};

} // namespace sensorhub
