#include "DataHub.h"

namespace sensorhub {

void DataHub::publish(const SensorReading& reading) {
    emit readingReceived(reading);
}

} // namespace sensorhub