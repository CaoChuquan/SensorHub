#include "DataHub.h"

namespace sensorhub {

void DataHub::publish(const SensorReading& reading) {
    emit readingReceived(reading);
}

void DataHub::publishStatus(const DeviceStatus& status) {
    emit deviceStatusReceived(status);
}

} // namespace sensorhub