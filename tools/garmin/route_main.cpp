#include "router.h"
#include <QCoreApplication>
#include <QCborValue>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>
#include <memory>

static QJsonObject readNetwork(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 512LL * 1024 * 1024) return {};
    const auto bytes = file.readAll();
    if (bytes.startsWith("VRCB1\n")) {
        QCborParserError error;
        const auto value = QCborValue::fromCbor(bytes.mid(6), &error);
        if (error.error != QCborError::NoError || !value.isMap()) return {};
        return value.toJsonValue().toObject();
    }
    return QJsonDocument::fromJson(bytes).object();
}
static QGeoCoordinate coordinate(const QJsonValue &value) {
    const auto pair = value.toArray();
    if (pair.size() != 2 || !pair[0].isDouble() || !pair[1].isDouble()) return {};
    return QGeoCoordinate(pair[1].toDouble(), pair[0].toDouble());
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const bool serve = argc == 3 && QString::fromLocal8Bit(argv[1]) == "--serve";
    if (!serve && argc != 8) return 1;
    auto write = [](const QJsonObject &value) {
        QTextStream stream(stdout);
        stream << QJsonDocument(value).toJson(QJsonDocument::Compact) << '\n';
        stream.flush();
    };
    const auto network = readNetwork(QString::fromLocal8Bit(argv[serve ? 2 : 1]));
    if (!network.value("complete").toBool() || !network.value("ways").isArray() ||
        !network.value("restrictions").isArray()) {
        write({{"error", "地图包含未解码或损坏的通行/时段限制，已停止规划以避免忽略限制。"}});
        return 3;
    }
    std::unique_ptr<RoutingGraph> graph;
    QString loadedMode;
    int graphBuilds = 0;
    auto route = [&](const QGeoCoordinate &start, const QGeoCoordinate &finish,
                     const QString &mode, const QString &strategy) {
        if (!graph || mode != loadedMode) {
            graph.reset(); // Keep at most one mode graph resident.
            graph = std::make_unique<RoutingGraph>(network.value("ways").toArray(),
                network.value("restrictions").toArray(), mode, true);
            loadedMode = mode;
            ++graphBuilds;
        }
        auto result = graph->route(start, finish, strategy);
        result.insert("graphBuilds", graphBuilds);
        return result;
    };
    if (!serve) {
        bool ok[4];
        double numbers[4];
        for (int i = 0; i < 4; ++i) numbers[i] = QString::fromLocal8Bit(argv[i + 2]).toDouble(&ok[i]);
        if (!ok[0] || !ok[1] || !ok[2] || !ok[3]) { write({{"error", "起终点经纬度无效。"}}); return 3; }
        const auto result = route(QGeoCoordinate(numbers[0], numbers[1]), QGeoCoordinate(numbers[2], numbers[3]),
                                  QString::fromLocal8Bit(argv[6]), QString::fromLocal8Bit(argv[7]));
        write(result);
        return result.contains("error") ? 3 : 0;
    }
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) return 2;
    while (true) {
        const auto line = input.readLine(65537);
        if (line.isEmpty()) break;
        if (line.size() > 65536 || !line.endsWith('\n')) {
            write({{"error", "路线请求无效或过大。"}}); return 3;
        }
        const auto request = QJsonDocument::fromJson(line).object();
        const auto start = coordinate(request.value("start")), finish = coordinate(request.value("finish"));
        const auto mode = request.value("mode").toString(), strategy = request.value("strategy").toString();
        if (!start.isValid() || !finish.isValid() ||
            (mode != "car" && mode != "walk" && mode != "bicycle") ||
            (strategy != "shortest" && strategy != "fastest")) {
            write({{"error", "路线请求参数无效。"}}); continue;
        }
        write(route(start, finish, mode, strategy));
    }
    return 0;
}
