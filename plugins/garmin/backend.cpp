#include "backend.h"
#include "runtimepaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

GarminBackend::GarminBackend(QObject *parent) : MapBackend(parent) { Q_INIT_RESOURCE(garmin_style); }
GarminBackend::~GarminBackend() { stopWorkers(); delete routeService; }

void GarminBackend::searchPlaces(const QString &query) {
    if (!places) { MapBackend::searchPlaces(query); return; }
    try { emit placesReady({{"items", places->search(query).toVariantList()}}); }
    catch (const std::exception &e) { emit placesReady({{"error", QString::fromUtf8(e.what())}}); }
}

void GarminBackend::load(const QUrl &url)
{
    if (importing) return;
    const QFileInfo file(url.toLocalFile());
    if (!url.isLocalFile() || !file.isFile() || !file.isReadable()) {
        emit prepared({{"error", tr("无法读取所选 IMG 地图。")}});
        return;
    }
    QString cacheError;
    cache = ImportCache::acquire(file.canonicalFilePath(), "garmin-import-v4", &cacheError);
    if (!cache) { emit prepared({{"error", cacheError}}); return; }
    const QString output = cache->path();
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
        const auto diagnostics = process->isOpen()
            ? QString::fromUtf8(process->readAllStandardError()).right(1500) : QString();
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
            if (!QFileInfo::exists(QDir(fontFolder).filePath(name)) &&
                !QFile::copy(":/offline/resources/fonts/" + name, QDir(fontFolder).filePath(name))) {
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
        routingFile = QDir(output).filePath("routing.cbor");
        const bool routes = QFileInfo::exists(routingFile) && metadata.value("routingWays").toInt() > 0
                            && metadata.value("routingRulesComplete").toBool();
        metadata.insert("routingAvailable", routes);
        metadata.insert("capabilities", QVariantMap{{"geometry", true}, {"tiles", false},
            {"search", true}, {"nearby", true}, {"routing", routes}, {"conditionalRouting", false}});
        try { places = std::make_unique<PlaceIndex>(QDir(output).filePath("places.sqlite"), false); }
        catch (const std::exception &e) { emit prepared({{"error", QString::fromUtf8(e.what())}}); return; }
        QString cacheError;
        if (!cache->commit({"metadata.json", "map.geojson", "places.sqlite", "routing.cbor"}, &cacheError)) {
            emit prepared({{"error", cacheError}}); return;
        }
        emit prepared(metadata);
    };
    connect(process, &QProcess::finished, this, [complete](int code, QProcess::ExitStatus status) {
        complete(code == 0 && status == QProcess::NormalExit);
    });
    connect(process, &QProcess::errorOccurred, this, [complete](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete(false);
    });
    if (cache->ready()) { QTimer::singleShot(0, this, [complete] { complete(true); }); return; }
    process->start(RuntimePaths::helper("mapviewer_garmin_import"),
                   {file.canonicalFilePath(), output, "--compact"});
    timeout->start(180000);
}

void GarminBackend::planRoute(const QGeoCoordinate &start, const QGeoCoordinate &finish,
                           const QString &mode, const QString &strategy)
{
    if (routingFile.isEmpty()) { MapBackend::planRoute(start, finish, mode, strategy); return; }
    if (!routeService) {
        routeService = new RouteService(this);
        connect(routeService, &RouteService::ready, this, &MapBackend::routeReady);
    }
    routeService->plan(routingFile, start, finish, mode, strategy);
}

QVariantMap GarminBackend::poiAt(const QGeoCoordinate &coordinate, double radiusMeters) const {
    if (!places) return MapBackend::poiAt(coordinate, radiusMeters);
    try { return places->nearby(coordinate, radiusMeters).toVariantMap(); }
    catch (const std::exception &e) { return {{"error", QString::fromUtf8(e.what())}}; }
}
