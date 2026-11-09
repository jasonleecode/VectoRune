#include "router.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 8)
        return 1;
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly))
        return 2;
    const auto document = QJsonDocument::fromJson(file.readAll());
    const auto network = document.object();
    if (!document.isObject() || !network.value("complete").toBool() ||
        !network.value("ways").isArray() || !network.value("restrictions").isArray()) {
        QTextStream(stdout)
            << "{\"error\":\"地图包含未解码或损坏的通行/时段限制，已停止规划以避免忽略限制。\"}";
        return 3;
    }
    const auto ways = network.value("ways").toArray();
    const auto result =
        planGarminRoute(ways, QGeoCoordinate(QString(argv[2]).toDouble(), QString(argv[3]).toDouble()),
                        QGeoCoordinate(QString(argv[4]).toDouble(), QString(argv[5]).toDouble()),
                        QString::fromLocal8Bit(argv[6]), QString::fromLocal8Bit(argv[7]),
                        network.value("restrictions").toArray(), true);
    QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact);
    return result.contains("error") ? 3 : 0;
}
