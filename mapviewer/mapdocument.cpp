#include "mapdocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <QCoreApplication>
#include <QProcess>
#include <QTimer>
#include <QGeoRectangle>
#include <cmath>

static QString searchKey(const QString &text) {
    QString result;
    for (auto c : text.normalized(QString::NormalizationForm_D).toCaseFolded())
        if (c.category() != QChar::Mark_NonSpacing) result += c;
    return result.trimmed();
}

void MapDocument::searchPlaces(const QString &query)
{
    const auto generation = ++searchGeneration;
    const auto term = searchKey(query);
    if (term.isEmpty()) { emit placesReady({{"items", QVariantList{}}}); return; }
    if (searchMapFile.isEmpty()) {
        QVariantList exact, prefix, other;
        for (const auto &value : poiIndex) {
            const auto poi = value.toObject();
            const auto name = searchKey(poi.value("name").toString());
            if (!name.contains(term)) continue;
            auto item = poi.toVariantMap();
            item.insert("subtitle", poi.value("category").toString() + " · " + poi.value("city").toString());
            if (name == term) exact.append(item);
            else if (name.startsWith(term)) prefix.append(item);
            else other.append(item);
        }
        exact.append(prefix); exact.append(other);
        while (exact.size() > 50) exact.removeLast();
        emit placesReady({{"items", exact}});
        return;
    }
    auto *process = new QProcess(this);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, process, [process] { process->kill(); });
    auto complete = [this, process, generation](bool ok) {
        const auto result = QJsonDocument::fromJson(process->readAllStandardOutput()).object().toVariantMap();
        process->deleteLater();
        if (generation != searchGeneration) return;
        emit placesReady(result.contains("items") || result.contains("error") ? result
            : QVariantMap{{"error", ok ? tr("搜索结果无效。") : tr("地图搜索失败或超时。")}});
    };
    connect(process, &QProcess::finished, this, [complete](int code, QProcess::ExitStatus status) { complete(code == 0 && status == QProcess::NormalExit); });
    connect(process, &QProcess::errorOccurred, this, [complete](QProcess::ProcessError error) { if (error == QProcess::FailedToStart) complete(false); });
    process->start(QCoreApplication::applicationDirPath() + "/mapviewer_place_search", {searchMapFile, query});
    timer->start(30000);
}

void MapDocument::load(const QUrl &url)
{
    if (importing) return;
    const QFileInfo file(url.toLocalFile());
    if (file.suffix().compare("img", Qt::CaseInsensitive) != 0) {
        const auto result = prepare(url);
        if (!result.contains("error")) { poiIndex = {}; routingFile.clear(); }
        emit prepared(result);
        return;
    }
    if (!url.isLocalFile() || !file.isFile() || !file.isReadable()) {
        emit prepared({{"error", tr("无法读取所选 IMG 地图。")}});
        return;
    }
    auto directory = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/mapviewer-garmin-XXXXXX");
    if (!directory->isValid()) {
        emit prepared({{"error", tr("无法创建地图转换目录。")}});
        return;
    }
    const QString output = directory->path();
    garminMaps.push_back(std::move(directory));
    auto *process = new QProcess(this);
    importing = true;
    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, &QProcess::kill);
    // An older C parser runs in its own process: malformed IMG files cannot
    // crash the viewer, and all of the parser's memory is released at exit.
    auto complete = [this, process, timeout, output, file](bool success) {
        if (!importing) return;
        importing = false;
        timeout->stop();
        const auto diagnostics = QString::fromUtf8(process->readAllStandardError()).right(1500);
        process->deleteLater();
        if (!success) {
            emit prepared({{"error", tr("libgarmin 无法解析此 IMG 地图，可能是格式不受支持、文件损坏或已加锁。\n%1").arg(diagnostics)}});
            return;
        }
        QFile metadataFile(QDir(output).filePath("metadata.json"));
        QFile styleTemplate(":/offline/resources/garmin-style.json");
        if (!metadataFile.open(QIODevice::ReadOnly) || !styleTemplate.open(QIODevice::ReadOnly)) {
            emit prepared({{"error", tr("无法读取佳明地图转换结果。")}});
            return;
        }
        auto metadata = QJsonDocument::fromJson(metadataFile.readAll()).object().toVariantMap();
        if (!metadata.contains("longitude") || !metadata.contains("latitude")) {
            emit prepared({{"error", tr("佳明地图没有有效的地理范围。")}});
            return;
        }
        auto style = QJsonDocument::fromJson(styleTemplate.readAll()).object();
        auto sources = style.value("sources").toObject();
        auto source = sources.value("garmin").toObject();
        source.insert("data", QUrl::fromLocalFile(QDir(output).filePath("map.geojson")).toString());
        sources.insert("garmin", source);
        style.insert("sources", sources);
        const QString fontFolder = QDir(output).filePath("glyphs/Noto Sans Regular");
        if (!QDir().mkpath(fontFolder)) {
            emit prepared({{"error", tr("无法创建本地字体目录。")}});
            return;
        }
        for (const auto &range : {"0-255", "256-511", "8192-8447"}) {
            const QString name = QString(range) + ".pbf";
            if (!QFile::copy(":/offline/resources/fonts/" + name, QDir(fontFolder).filePath(name))) {
                emit prepared({{"error", tr("无法读取离线字形资源。")}});
                return;
            }
        }
        style.insert("glyphs", QUrl::fromLocalFile(output).toString() + "/glyphs/{fontstack}/{range}.pbf");
        QFile styleFile(QDir(output).filePath("style.json"));
        const auto bytes = QJsonDocument(style).toJson();
        if (!styleFile.open(QIODevice::WriteOnly) || styleFile.write(bytes) != bytes.size() || !styleFile.flush()) {
            emit prepared({{"error", tr("无法创建佳明地图样式。")}});
            return;
        }
        metadata.insert("styleUrl", QUrl::fromLocalFile(styleFile.fileName()).toString());
        metadata.insert("name", file.completeBaseName());
        metadata.insert("folder", QUrl::fromLocalFile(file.absolutePath()));
        metadata.insert("zoom", 11);
        metadata.insert("attribution", "Garmin IMG · libgarmin");
        routingFile = QDir(output).filePath("routing.json");
        searchMapFile.clear();
        ++searchGeneration;
        metadata.insert("routingAvailable", QFileInfo::exists(routingFile) && metadata.value("routingWays").toInt() > 0);
        QFile pois(QDir(output).filePath("pois.json"));
        if (pois.open(QIODevice::ReadOnly))
            poiIndex = QJsonDocument::fromJson(pois.readAll()).array();
        else poiIndex = {};
        emit prepared(metadata);
    };
    connect(process, &QProcess::finished, this, [complete](int code, QProcess::ExitStatus status) {
        complete(code == 0 && status == QProcess::NormalExit);
    });
    connect(process, &QProcess::errorOccurred, this, [complete](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete(false);
    });
    process->start(QCoreApplication::applicationDirPath() + "/mapviewer_garmin_import",
                   {file.canonicalFilePath(), output});
    timeout->start(180000);
}

void MapDocument::planRoute(const QGeoCoordinate &start, const QGeoCoordinate &finish,
                           const QString &mode, const QString &strategy)
{
    if (routing) return;
    if (routingFile.isEmpty()) {
        emit routeReady({{"error", tr("请先加载包含路网的佳明 IMG 地图。")}});
        return;
    }
    routing = true;
    const auto requestedMap = routingFile;
    auto *process = new QProcess(this);
    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, &QProcess::kill);
    auto complete = [this, process, timeout, requestedMap](bool success) {
        if (!routing) return;
        routing = false;
        timeout->stop();
        const auto json = QJsonDocument::fromJson(process->readAllStandardOutput()).object();
        process->deleteLater();
        if (requestedMap != routingFile) {
            emit routeReady({{"error", tr("地图已更换，请重新规划路线。")}});
            return;
        }
        if (json.contains("error")) { emit routeReady(json.toVariantMap()); return; }
        if (!success || !json.contains("coordinates")) {
            emit routeReady({{"error", tr("路线规划进程失败或超时。")}});
            return;
        }
        auto result = json.toVariantMap();
        QVariantList path;
        QList<QGeoCoordinate> coordinates;
        for (const auto &value : json.value("coordinates").toArray()) {
            const auto pair = value.toArray();
            const QGeoCoordinate coordinate(pair[1].toDouble(), pair[0].toDouble());
            path.append(QVariant::fromValue(coordinate));
            coordinates.append(coordinate);
        }
        result.insert("path", path);
        QGeoRectangle bounds(coordinates);
        bounds.setWidth(qMax(0.001, bounds.width() * 1.15));
        bounds.setHeight(qMax(0.001, bounds.height() * 1.15));
        result.insert("bounds", QVariant::fromValue(bounds));
        emit routeReady(result);
    };
    connect(process, &QProcess::finished, this, [complete](int code, QProcess::ExitStatus status) {
        complete(code == 0 && status == QProcess::NormalExit);
    });
    connect(process, &QProcess::errorOccurred, this, [complete](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete(false);
    });
    auto number = [](double value) { return QString::number(value, 'g', 16); };
    process->start(QCoreApplication::applicationDirPath() + "/mapviewer_garmin_route",
        {routingFile, number(start.latitude()), number(start.longitude()),
         number(finish.latitude()), number(finish.longitude()), mode, strategy});
    timeout->start(60000);
}

QVariantMap MapDocument::poiAt(const QGeoCoordinate &coordinate, double radiusMeters) const
{
    if (poiIndex.isEmpty())
        return {{"error", tr("当前地图没有可查询的佳明地点信息。")}};
    if (!coordinate.isValid() || !std::isfinite(radiusMeters) || radiusMeters <= 0)
        return {{"error", tr("查询位置无效。")}};
    double nearestDistance = qMin(radiusMeters, 10000.0);
    QJsonObject nearest;
    for (const auto &value : poiIndex) {
        const auto poi = value.toObject();
        const QGeoCoordinate location(poi.value("latitude").toDouble(), poi.value("longitude").toDouble());
        const double distance = coordinate.distanceTo(location);
        if (distance <= nearestDistance) { nearestDistance = distance; nearest = poi; }
    }
    if (nearest.isEmpty()) return {{"error", tr("附近没有地点，请靠近 POI 点后重试。")}};
    auto result = nearest.toVariantMap();
    result.insert("distance", nearestDistance);
    return result;
}

QVariantMap MapDocument::prepare(const QUrl &url)
{
    auto fail = [](const QString &message) { return QVariantMap{{"error", message}}; };
    const QFileInfo file(url.toLocalFile());
    if (!url.isLocalFile() || !file.isFile() || !file.isReadable())
        return fail(tr("无法读取所选地图文件。"));

    QVariantMap metadata;
    bool validTiles = false;
    const auto connection = QUuid::createUuid().toString();
    {
        auto database = QSqlDatabase::addDatabase("QSQLITE", connection);
        database.setDatabaseName(file.canonicalFilePath());
        database.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (database.open()) {
            QSqlQuery query(database);
            if (query.exec("SELECT name, value FROM metadata")) {
                while (query.next())
                    metadata.insert(query.value(0).toString(), query.value(1));
            }
            validTiles = query.exec("SELECT zoom_level, tile_column, tile_row, tile_data FROM tiles LIMIT 1")
                         && query.next();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    if (!validTiles || metadata.isEmpty())
        return fail(tr("文件不是有效的 MBTiles 地图，或地图中没有瓦片。"));
    const auto format = metadata.value("format").toString().toLower();
    if (format != "pbf" && format != "png" && format != "jpg" && format != "jpeg" && format != "webp")
        return fail(tr("不支持此瓦片格式：%1").arg(format));

    const auto tileUrl = "mbtiles://" + QString::fromLatin1(
        QUrl::toPercentEncoding(file.canonicalFilePath(), "/"));
    QJsonObject style;
    if (format == "pbf") {
        const auto layers = QJsonDocument::fromJson(metadata.value("json").toString().toUtf8())
                                .object().value("vector_layers").toArray();
        bool compatible = false;
        for (const auto &layer : layers)
            compatible |= layer.toObject().value("id").toString() == "transportation";
        if (!compatible)
            return fail(tr("此矢量地图与当前样式不兼容，需要 OpenMapTiles 图层结构。"));
        QFile styleTemplate(":/offline/resources/beijing-style.json");
        if (!styleTemplate.open(QIODevice::ReadOnly))
            return fail(tr("无法读取地图样式。"));
        style = QJsonDocument::fromJson(styleTemplate.readAll()).object();
        auto sources = style.value("sources").toObject();
        auto source = sources.value("beijing").toObject();
        source.insert("url", tileUrl);
        sources.insert("beijing", source);
        style.insert("sources", sources);
    } else {
        style = QJsonObject{{"version", 8},
            {"sources", QJsonObject{{"map", QJsonObject{{"type", "raster"}, {"url", tileUrl},
                {"tileSize", 256}, {"attribution", metadata.value("attribution").toString()}}}}},
            {"layers", QJsonArray{QJsonObject{{"id", "map"}, {"type", "raster"}, {"source", "map"}}}}};
    }
    auto styleFile = std::make_unique<QTemporaryFile>(QDir::tempPath() + "/mapviewer-style-XXXXXX.json");
    const auto bytes = QJsonDocument(style).toJson();
    if (!styleFile->open() || styleFile->write(bytes) != bytes.size() || !styleFile->flush())
        return fail(tr("无法创建地图样式临时文件。"));
    const auto styleUrl = QUrl::fromLocalFile(styleFile->fileName()).toString();
    styles.push_back(std::move(styleFile));

    double longitude = 116.4074, latitude = 39.9042, zoom = 12;
    const auto bounds = metadata.value("bounds").toString().split(',');
    if (bounds.size() == 4) {
        bool ok[4];
        double values[4];
        for (int i = 0; i < 4; ++i)
            values[i] = bounds[i].toDouble(&ok[i]);
        if (ok[0] && ok[1] && ok[2] && ok[3]) {
            longitude = (values[0] + values[2]) / 2;
            latitude = (values[1] + values[3]) / 2;
        }
    }
    const auto center = metadata.value("center").toString().split(',');
    if (center.size() >= 2) {
        bool lonOk, latOk;
        const double lon = center[0].toDouble(&lonOk), lat = center[1].toDouble(&latOk);
        if (lonOk && latOk) { longitude = lon; latitude = lat; }
        if (center.size() >= 3) {
            bool ok;
            const double z = center[2].toDouble(&ok);
            if (ok && std::isfinite(z)) zoom = qBound(0.0, z, 20.0);
        }
    }
    if (!std::isfinite(longitude) || !std::isfinite(latitude)
        || std::abs(longitude) > 180 || std::abs(latitude) > 85.051129) {
        longitude = 116.4074;
        latitude = 39.9042;
    }
    searchMapFile = file.canonicalFilePath();
    ++searchGeneration;
    return {{"styleUrl", styleUrl}, {"name", file.completeBaseName()},
            {"attribution", metadata.value("attribution", "© OpenStreetMap contributors")},
            {"folder", QUrl::fromLocalFile(file.absolutePath())},
            {"latitude", latitude}, {"longitude", longitude}, {"zoom", zoom}};
}
