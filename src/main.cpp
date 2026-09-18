#include <QApplication>
#include <QDateTime>
#include <QLabel>
#include <QMainWindow>
#include <QStringLiteral>

#include "core/DataHub.h"
#include "data/SensorTypes.h"
#include "data/SimulationProducer.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 注册自定义类型到 Qt 元类型系统
    qRegisterMetaType<sensorhub::SensorReading>("sensorhub::SensorReading");

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("SensorHub"));
    window.resize(900, 600);

    // 临时用 QLabel 当实时显示面板
    auto* view = new QLabel(QStringLiteral("等待机房探头数据...\n"));
    view->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    view->setStyleSheet(QStringLiteral(
        "font-family: Consolas, 'Courier New', monospace;"
        "font-size: 14px; padding: 8px;"));
    window.setCentralWidget(view);

    sensorhub::DataHub hub;
    sensorhub::SimulationProducer producer;

    QObject::connect(&producer, &sensorhub::SimulationProducer::newReading,
                     &hub, &sensorhub::DataHub::publish);
    QObject::connect(&hub, &sensorhub::DataHub::readingReceived,
                     view, [&view](const sensorhub::SensorReading& r) {
        view->setText(view->text() + QStringLiteral("[%1] %2/%3 = %4\n")
            .arg(QDateTime::fromMSecsSinceEpoch(r.timestampMs).toString("hh:mm:ss"))
            .arg(r.sensorId, r.type)
            .arg(r.value, 0, 'f', 2));
    });

    // 开启每秒采样
    producer.start();
    window.show();
    
    return app.exec();
}