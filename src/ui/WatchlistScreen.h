#pragma once

#include "data/SensorTypes.h"
#include <QHash>
#include <QPushButton>
#include <QTableWidget>
#include <QVector>
#include <QWidget>

namespace sensorhub {

class DataHub;
class WatchlistService;

class WatchlistScreen : public QWidget {
	Q_OBJECT
public:
	explicit WatchlistScreen(DataHub& hub, WatchlistService& service,
							 QWidget* parent = nullptr);
public slots:
	// 重新从服务加载关注列表，重建表格行和 key→行号 映射
	void reloadFromDb();
private slots:
	void onAddClicked();	    // 添加关注
    void onRemoveClicked();     // 删除选中行
	void onReading(const SensorReading& reading); // 处理读数
private:
	void setupUi();

	// UI 元素
	QTableWidget* table_ = nullptr;
	QPushButton* btnAdd_ = nullptr;
	QPushButton* btnRemove_ = nullptr;
	QPushButton* btnRefresh_ = nullptr;

	DataHub& hub_;
	WatchlistService& service_;

	//当前表格显示的条目
	QVector<WatchlistEntry> entry_;
	// "sensorId|type" → 行号 的映射
	QHash<QString, int> keyToRow_;

	// 列索引
	enum Column {
		ColId = 0,    // 数据库 ID
		ColSensor,    // 传感器 ID
		ColType,      // 指标类型
		ColLabel,     // 用户标签
		ColAddedAt,   // 添加时间
		ColLatest,    // 最新读数（实时刷新）
		ColCount      // 列总数
	};
};

} // namespace sensorhub