#include <memory>
#include <QApplication>
#include <QDebug>
#include <QStringLiteral>
#include <QCommandLineParser>

#include "core/DataHub.h"
#include "data/SensorTypes.h"
#include "data/SimulationProducer.h"
#include "data/AlarmTypes.h"
#include "data/ISensorProducer.h"
#include "data/DeviceConfig.h"
#include "data/DeviceConfigLoader.h"
#include "data/ModbusProducer.h"
#include "storage/Database.h"
#include "ui/MainWindow.h"
#include "services/WatchlistService.h"
#include "services/AnalysisService.h"
#include "services/ChartService.h"
#include "services/AlarmService.h"
#include "services/SensorArchiveService.h"

using namespace sensorhub;

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SensorHub"));

    // 命令行参数：决定用哪种数据源
    // 不填 --modbus 就用仿真数据
    // 填了就从真实设备读，例如：SensorHub.exe --modbus 192.168.1.100:502
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("SensorHub 机房环境监控终端"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption modbusOption(
        QStringLiteral("modbus"),
        QStringLiteral("从 Modbus TCP 设备采集数据。可跟地址 IP[:端口]（默认 502），"
                       "省略时使用 config/devices.json 里的地址。"
                       "不填该参数则使用仿真数据。"));
    parser.addOption(modbusOption);
    parser.process(app);

    // 注册自定义类型到 Qt 元类型系统
    qRegisterMetaType<sensorhub::SensorReading>("sensorhub::SensorReading");
    qRegisterMetaType<sensorhub::AnalysisStats>("sensorhub::AnalysisStats");
    qRegisterMetaType<QVector<sensorhub::AnomalyPoint>>(
        "QVector<sensorhub::AnomalyPoint>");
    qRegisterMetaType<sensorhub::AlarmEvent>("sensorhub::AlarmEvent");

    // 建数据库
    Database database;
    if (!database.open()) {
        qWarning() << QStringLiteral("数据库打开失败:")
                   << database.lastError()
                   << QStringLiteral("（存档功能不可用，实时显示不受影响）");
    }

    // 关注列表服务
    WatchlistService watchlistService(database);
    // 统计分析服务
    AnalysisService analysisService(database);
    // 时序图服务
    ChartService chartService(database);

    // 创建 DataHub
    DataHub hub;

    // 消费者：存档服务
    SensorArchiveService archive(hub, database);
    // 告警服务
    AlarmService alarmService(database, hub);

    // 创建 MainWindow 界面
    MainWindow window(hub, watchlistService, analysisService, chartService,
                      alarmService);

    // 生产者
    std::unique_ptr<ISensorProducer> producer = 
        std::make_unique<SimulationProducer>();

    if (!parser.isSet(modbusOption)) {
        producer = std::make_unique<sensorhub::SimulationProducer>();
        qInfo() << "数据源：仿真（未指定 --modbus）";
    } 
    else {
        // 设备表优先从 exe 旁的 config/devices.json 读（构建时会自动复制过去）；
        // 文件缺失或写错时回退到内置默认配置，保证程序仍然能跑。
        const QString configPath =
            QCoreApplication::applicationDirPath() + QStringLiteral("/config/devices.json");
        QString configError;
        QVector<sensorhub::DeviceConfig> devices =
            sensorhub::DeviceConfigLoader::loadFromFile(configPath, &configError);
        if (devices.isEmpty()) {
            qWarning().noquote() << "设备表加载失败：" << configError
                                 << "，改用内置默认配置";
            devices = sensorhub::DeviceConfigLoader::defaults();
        }

        // 地址可以写在配置里，也可以在命令行临时覆盖
        const QString modbusAddress = parser.positionalArguments().value(0).trimmed();
        if (!modbusAddress.isEmpty()) {
            QString host = modbusAddress;
            quint16 port = 502;
            const int colon = host.lastIndexOf(QLatin1Char(':'));
            if (colon > 0) {
                bool ok = false;
                const uint parsed = host.mid(colon + 1).toUInt(&ok);
                if (ok && parsed > 0 && parsed <= 65535) {
                    port = static_cast<quint16>(parsed);
                    host = host.left(colon);
                }
            }
            for (sensorhub::DeviceConfig& device : devices) {
                device.host = host;
                device.port = port;
            }
        }

        qInfo() << "数据源：Modbus TCP" << devices.first().host
                << devices.first().port << "，共" << devices.size() << "台设备";
        producer = std::make_unique<sensorhub::ModbusProducer>(devices);
    }

    // 告诉界面数据的来源
    window.setDataSourceLabel(producer->sourceDescription());    

    QObject::connect(producer.get(), &ISensorProducer::newReading,
                     &hub, &DataHub::publish);

    QObject::connect(producer.get(), &ISensorProducer::linkStateChanged,
                     &window, &MainWindow::setLinkState);

    // 启动生产者和窗口
    producer->start();
    window.show();
    
    // 进入 Qt 事件循环，窗口关闭后返回
    const int exitCode = app.exec();

    // 先主动停掉采集，再让下面的栈对象析构：
    // 这样退出瞬间不会再有读数进来，也不会误报"设备掉线"
    producer->stop();

    return exitCode;
}