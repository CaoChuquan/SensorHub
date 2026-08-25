#include <QApplication>
#include <QDateTime>
#include <QLabel>
#include <QMainWindow>
#include <QStringLiteral>

#include "data/SensorTypes.h"
#include "data/SimulationProducer.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SensorHub"));
    app.setApplicationVersion(QStringLiteral("0.2.0"));

    qRegisterMetaType<sensorhub::SensorReading>("sensorhub::SensorReading");

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("SensorHub - 物联网数据采集终端"));
    window.resize(900, 600);

    //用 QLabel 当实时显示面板，先跑通"数据源 -> UI"这条链
    auto* view = new QLabel(QStringLiteral("等待数据...\n"));
    view->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    view->setStyleSheet(QStringLiteral(
        "font-family: Consolas, 'Courier New', monospace;"
        "font-size: 14px; padding: 8px;"));
    window.setCentralWidget(view);

    // 数据源：仿真生产者。本步直接把它的信号连到 UI
    // 只有一个消费者，直连最简单；等出现第二个消费者（存档/告警）时再引入数据平面
    sensorhub::SimulationProducer producer;
    QObject::connect(&producer, &sensorhub::SimulationProducer::newReading,
        view, [&view](const sensorhub::SensorReading& r) {
            view->setText(view->text() +
                QStringLiteral("[%1] %2/%3 = %4\n")
                    .arg(QDateTime::fromMSecsSinceEpoch(r.timestampMs).toString("hh:mm:ss"))
                    .arg(r.sensorId, r.type)
                    .arg(r.value, 0, 'f', 2));
        });

    producer.start(); // 开启每秒采样
    window.show();
    return app.exec();
}