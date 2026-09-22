#include <QApplication>
#include <QDebug>
#include <QStringLiteral>

#include "core/DataHub.h"
#include "data/SensorTypes.h"
#include "data/SimulationProducer.h"
#include "services/SensorArchiveService.h"
#include "storage/Database.h"
#include "ui/MainWindow.h"

using namespace sensorhub;

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 注册自定义类型到 Qt 元类型系统
    qRegisterMetaType<sensorhub::SensorReading>("sensorhub::SensorReading");

    // 建数据库
    Database database;
    if (!database.open()) {
        qWarning() << QStringLiteral("数据库打开失败:")
                   << database.lastError()
                   << QStringLiteral("（存档功能不可用，实时显示不受影响）");
    }

    // 创建 DataHub
    DataHub hub;

    // 消费者：存档服务
    SensorArchiveService archive(hub, database);

    // 创建界面
    MainWindow window(hub);
    
    // 生产者
    sensorhub::SimulationProducer producer;
    QObject::connect(&producer, &SimulationProducer::newReading,
                     &hub, &DataHub::publish);
      
    // 启动生产者和窗口
    producer.start();
    window.show();                 

    return app.exec();
}