#include "WatchlistService.h"
#include "storage/WatchlistRepository.h"
#include "storage/Database.h"

namespace sensorhub {

	WatchlistService::WatchlistService(Database& db, QObject* parent)
        : QObject(parent), we_(new WatchlistRepository(db)) {
	}

	WatchlistService::~WatchlistService() {
		delete we_;
	}

	QVector<WatchlistEntry> WatchlistService::loadAll() {
		return we_->loadAll();
	}

	bool WatchlistService::add(const QString& sensorId, const QString& type,
		const QString& label)
	{
		WatchlistEntry e;
		e.sensorId = sensorId;
		e.type = type;
		e.label = label;

		if (we_->add(e)) {
			// 成功后通知 UI 刷新——这是 Service 作为"中介"的核心价值：
			// UI 不需要自己记得在 add 后调 reload，只需连接这个信号。
			emit watchlistChanged();
			return true;
		}
		return false;
	}

	bool WatchlistService::removeById(int id) {
		if (we_->removeById(id)) {
			emit watchlistChanged();
			return true;
		}
		return false;
	}

} // namesapce sensorhub