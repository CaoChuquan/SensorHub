#include "DeviceConfigLoader.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

namespace sensorhub {

namespace {

// 统一的失败出口：写错误信息（调用方可能不关心，error 允许为空）
bool fail(QString* error, const QString& message) {
    if (error) {
        *error = message;
    }
    return false;
}

// 结构体层面的校验。字段类型对不对在解析时已经查过了，
// 这里只查"值本身合不合理"，比如重名、从站号越界
bool validate(const QVector<DeviceConfig>& devices, QString* error) {
    if (devices.isEmpty()) {
        return fail(error, QStringLiteral("devices 数组是空的，至少要配一台设备"));
    }

    QSet<QString> names; // 机柜名会作为 sensorId，重名会让两台的读数混在一起
    for (int i = 0; i < devices.size(); ++i) {
        const DeviceConfig& device = devices.at(i);
        const QString where = QStringLiteral("devices[%1]").arg(i);

        if (device.name.isEmpty()) {
            return fail(error, where + QStringLiteral(" 缺少 name"));
        }
        if (names.contains(device.name)) {
            return fail(error, QStringLiteral("设备名重复：%1（%2）").arg(device.name, where));
        }
        names.insert(device.name);

        if (device.host.isEmpty()) {
            return fail(error, where + QStringLiteral(" 缺少 host"));
        }
        if (device.unitId < 1 || device.unitId > 247) {
            // Modbus 规范的从站地址范围，0 是广播地址，248 以上保留
            return fail(error, QStringLiteral("%1 的 unitId 必须在 1-247 之间，当前 %2")
                                   .arg(where).arg(device.unitId));
        }
        if (device.points.isEmpty()) {
            return fail(error, where + QStringLiteral(" 至少需要配置一个采集点"));
        }

        QSet<QString> types; // 同一台设备里同一个指标只能有一个来源
        for (int j = 0; j < device.points.size(); ++j) {
            const DevicePoint& point = device.points.at(j);
            const QString pointWhere = QStringLiteral("%1.points[%2]").arg(where).arg(j);

            if (point.type.isEmpty()) {
                return fail(error, pointWhere + QStringLiteral(" 缺少 type"));
            }
            if (types.contains(point.type)) {
                return fail(error, QStringLiteral("%1 的 type 重复：%2")
                                       .arg(pointWhere, point.type));
            }
            types.insert(point.type);

            if (point.registerAddress < 0 || point.registerAddress > 65535) {
                return fail(error, QStringLiteral("%1 的 register 必须在 0-65535 之间")
                                       .arg(pointWhere));
            }
            if (point.scale <= 0.0) {
                return fail(error, pointWhere + QStringLiteral(" 的 scale 必须大于 0"));
            }
        }
    }
    return true;
}

// 解析单台设备。这里只负责"结构对不对"，值合不合理交给 validate
bool parseDevice(const QJsonValue& value, int index, DeviceConfig* out, QString* error) {
    const QString where = QStringLiteral("devices[%1]").arg(index);
    if (!value.isObject()) {
        return fail(error, where + QStringLiteral(" 必须是一个对象"));
    }
    const QJsonObject object = value.toObject();

    out->name = object.value(QStringLiteral("name")).toString().trimmed();
    out->host = object.value(QStringLiteral("host")).toString().trimmed();

    // port / unitId 不给默认值：连错端口或从站号读出来的数据是"别人的"，
    // 比直接报错更难排查
    const QJsonValue portValue = object.value(QStringLiteral("port"));
    if (!portValue.isDouble()) {
        return fail(error, where + QStringLiteral(" 缺少数字类型的 port"));
    }
    out->port = static_cast<quint16>(portValue.toInt());

    const QJsonValue unitValue = object.value(QStringLiteral("unitId"));
    if (!unitValue.isDouble()) {
        return fail(error, where + QStringLiteral(" 缺少数字类型的 unitId"));
    }
    out->unitId = unitValue.toInt();

    const QJsonValue pointsValue = object.value(QStringLiteral("points"));
    if (!pointsValue.isArray()) {
        return fail(error, where + QStringLiteral(" 缺少 points 数组"));
    }

    const QJsonArray points = pointsValue.toArray();
    for (int j = 0; j < points.size(); ++j) {
        const QJsonValue pointValue = points.at(j);
        const QString pointWhere = QStringLiteral("%1.points[%2]").arg(where).arg(j);
        if (!pointValue.isObject()) {
            return fail(error, pointWhere + QStringLiteral(" 必须是一个对象"));
        }
        const QJsonObject pointObject = pointValue.toObject();

        DevicePoint point;
        point.type = pointObject.value(QStringLiteral("type")).toString().trimmed();
        point.unit = pointObject.value(QStringLiteral("unit")).toString();

        const QJsonValue registerValue = pointObject.value(QStringLiteral("register"));
        if (!registerValue.isDouble()) {
            return fail(error, pointWhere + QStringLiteral(" 缺少数字类型的 register"));
        }
        point.registerAddress = registerValue.toInt();

        // scale 允许省略，默认 1.0（寄存器里就是实际值）
        const QJsonValue scaleValue = pointObject.value(QStringLiteral("scale"));
        if (!scaleValue.isUndefined()) {
            if (!scaleValue.isDouble()) {
                return fail(error, pointWhere + QStringLiteral(" 的 scale 必须是数字"));
            }
            point.scale = scaleValue.toDouble();
        }

        out->points.append(point);
    }
    return true;
}

} // namespace

QVector<DeviceConfig> DeviceConfigLoader::loadFromFile(const QString& path, QString* error) {
    if (error) {
        error->clear();
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, QStringLiteral("打不开配置文件：%1").arg(path));
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        fail(error, QStringLiteral("JSON 语法错误（第 %1 字节）：%2")
                        .arg(parseError.offset)
                        .arg(parseError.errorString()));
        return {};
    }
    if (!document.isObject()) {
        fail(error, QStringLiteral("配置文件的根节点必须是一个对象"));
        return {};
    }

    const QJsonValue devicesValue = document.object().value(QStringLiteral("devices"));
    if (!devicesValue.isArray()) {
        fail(error, QStringLiteral("缺少 devices 数组"));
        return {};
    }

    QVector<DeviceConfig> devices;
    const QJsonArray array = devicesValue.toArray();
    for (int i = 0; i < array.size(); ++i) {
        DeviceConfig device;
        if (!parseDevice(array.at(i), i, &device, error)) {
            return {};
        }
        devices.append(device);
    }

    if (!validate(devices, error)) {
        return {};
    }
    return devices;
}

QVector<DeviceConfig> DeviceConfigLoader::defaults() {
    // 和 config/devices.json 保持一致：本机模拟器，3 台机柜 3 个从站
    QVector<DeviceConfig> devices;
    for (int i = 1; i <= 3; ++i) {
        DeviceConfig device;
        device.name = QStringLiteral("rack%1").arg(i);
        device.host = QStringLiteral("127.0.0.1");
        device.port = 502;
        device.unitId = i;
        device.points = {
            {QStringLiteral("temperature"), 0, 0.1, QStringLiteral("℃")},
            {QStringLiteral("humidity"),    1, 0.1, QStringLiteral("%RH")},
            {QStringLiteral("voltage"),     2, 0.1, QStringLiteral("V")},
        };
        devices.append(device);
    }
    return devices;
}

} // namespace sensorhub