// devices.json 加载器的单元测试
//
// 配置文件是"用户手写的东西"，最容易出的错不是程序崩，而是悄悄读出一个
// 看起来正常的错误值。这里把几类典型错误固定下来：
//   文件不存在、JSON 语法错、设备重名、同设备点表 type 重复、从站号越界
// 同时验证合法文件能完整读出来（包括省略 scale 时的默认值）
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "data/DeviceConfigLoader.h"

using namespace sensorhub;

class TstDeviceConfig : public QObject {
    Q_OBJECT
private slots:
    void loadsValidFile();
    void defaultsAreUsable();
    void missingFileReportsError();
    void invalidJsonReportsError();
    void duplicateDeviceNameRejected();
    void duplicatePointTypeRejected();
    void invalidUnitIdRejected();
private:
    // 写一个临时 JSON 文件，返回路径；写失败返回空字符串
    QString writeConfig(const QString& fileName, const QByteArray& content);

    QTemporaryDir dir_;
};

QString TstDeviceConfig::writeConfig(const QString& fileName,
                                     const QByteArray& content) {
    const QString path = dir_.filePath(fileName);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return QString();
    }
    file.write(content);
    return path;
}

void TstDeviceConfig::loadsValidFile() {
    const QString path = writeConfig(QStringLiteral("valid.json"), R"({
        "devices": [
            {
                "name": "rack1",
                "host": "10.0.0.5",
                "port": 502,
                "unitId": 1,
                "points": [
                    { "type": "temperature", "register": 0, "scale": 0.1, "unit": "℃" }
                ]
            },
            {
                "name": "rack2",
                "host": "10.0.0.6",
                "port": 1502,
                "unitId": 7,
                "points": [
                    { "type": "humidity", "register": 3 }
                ]
            }
        ]
    })");
    QVERIFY(!path.isEmpty());

    QString error;
    const QVector<DeviceConfig> devices = DeviceConfigLoader::loadFromFile(path, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(devices.size(), 2);

    QCOMPARE(devices.at(0).name, QStringLiteral("rack1"));
    QCOMPARE(devices.at(0).host, QStringLiteral("10.0.0.5"));
    QCOMPARE(devices.at(0).port, quint16(502));
    QCOMPARE(devices.at(0).unitId, 1);
    QCOMPARE(devices.at(0).points.size(), 1);
    QCOMPARE(devices.at(0).points.first().type, QStringLiteral("temperature"));
    QCOMPARE(devices.at(0).points.first().registerAddress, 0);
    QVERIFY(qAbs(devices.at(0).points.first().scale - 0.1) < 1e-9);
    QCOMPARE(devices.at(0).points.first().unit, QStringLiteral("℃"));

    QCOMPARE(devices.at(1).name, QStringLiteral("rack2"));
    QCOMPARE(devices.at(1).port, quint16(1502));
    QCOMPARE(devices.at(1).unitId, 7);
    // 没写 scale 时应取默认值 1.0
    QVERIFY(qAbs(devices.at(1).points.first().scale - 1.0) < 1e-9);
}

void TstDeviceConfig::defaultsAreUsable() {
    const QVector<DeviceConfig> devices = DeviceConfigLoader::defaults();

    QCOMPARE(devices.size(), 3);
    for (int i = 0; i < devices.size(); ++i) {
        QCOMPARE(devices.at(i).name, QStringLiteral("rack%1").arg(i + 1));
        QCOMPARE(devices.at(i).host, QStringLiteral("127.0.0.1"));
        QCOMPARE(devices.at(i).port, quint16(502));
        QCOMPARE(devices.at(i).unitId, i + 1);
        QCOMPARE(devices.at(i).points.size(), 3);
        QCOMPARE(devices.at(i).points.at(0).type, QStringLiteral("temperature"));
        QCOMPARE(devices.at(i).points.at(1).type, QStringLiteral("humidity"));
        QCOMPARE(devices.at(i).points.at(2).type, QStringLiteral("voltage"));
    }
}

void TstDeviceConfig::missingFileReportsError() {
    QString error;
    const QVector<DeviceConfig> devices =
        DeviceConfigLoader::loadFromFile(dir_.filePath(QStringLiteral("nope.json")), &error);

    QVERIFY(devices.isEmpty());
    QVERIFY(!error.isEmpty());
}

void TstDeviceConfig::invalidJsonReportsError() {
    const QString path = writeConfig(QStringLiteral("broken.json"),
                                     QByteArrayLiteral("{ \"devices\": [ }"));
    QVERIFY(!path.isEmpty());

    QString error;
    const QVector<DeviceConfig> devices = DeviceConfigLoader::loadFromFile(path, &error);

    QVERIFY(devices.isEmpty());
    QVERIFY(error.contains(QStringLiteral("JSON")));
}

void TstDeviceConfig::duplicateDeviceNameRejected() {
    const QString path = writeConfig(QStringLiteral("dup-name.json"), R"({
        "devices": [
            { "name": "rack1", "host": "127.0.0.1", "port": 502, "unitId": 1,
              "points": [ { "type": "temperature", "register": 0 } ] },
            { "name": "rack1", "host": "127.0.0.1", "port": 502, "unitId": 2,
              "points": [ { "type": "temperature", "register": 0 } ] }
        ]
    })");
    QVERIFY(!path.isEmpty());

    QString error;
    const QVector<DeviceConfig> devices = DeviceConfigLoader::loadFromFile(path, &error);

    QVERIFY(devices.isEmpty());
    QVERIFY(error.contains(QStringLiteral("重复")));
}

void TstDeviceConfig::duplicatePointTypeRejected() {
    const QString path = writeConfig(QStringLiteral("dup-type.json"), R"({
        "devices": [
            { "name": "rack1", "host": "127.0.0.1", "port": 502, "unitId": 1,
              "points": [
                  { "type": "temperature", "register": 0 },
                  { "type": "temperature", "register": 1 }
              ] }
        ]
    })");
    QVERIFY(!path.isEmpty());

    QString error;
    const QVector<DeviceConfig> devices = DeviceConfigLoader::loadFromFile(path, &error);

    QVERIFY(devices.isEmpty());
    QVERIFY(error.contains(QStringLiteral("重复")));
}

void TstDeviceConfig::invalidUnitIdRejected() {
    // Modbus 从站号 0 是广播地址，不能用作单台设备的标识
    const QString path = writeConfig(QStringLiteral("bad-unit.json"), R"({
        "devices": [
            { "name": "rack1", "host": "127.0.0.1", "port": 502, "unitId": 0,
              "points": [ { "type": "temperature", "register": 0 } ] }
        ]
    })");
    QVERIFY(!path.isEmpty());

    QString error;
    const QVector<DeviceConfig> devices = DeviceConfigLoader::loadFromFile(path, &error);

    QVERIFY(devices.isEmpty());
    QVERIFY(error.contains(QStringLiteral("unitId")));
}

QTEST_GUILESS_MAIN(TstDeviceConfig)
#include "tst_deviceconfig.moc"