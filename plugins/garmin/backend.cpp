#include "backend.h"
#include "runtimepaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QTimer>

GarminBackend::GarminBackend(QObject *parent) : MapBackend(parent) { Q_INIT_RESOURCE(garmin_style); }
GarminBackend::~GarminBackend() { stopWorkers(); delete routeService; }

void GarminBackend::searchPlaces(const QString &query) {
    if (!places) { MapBackend::searchPlaces(query); return; }
    try { emit placesReady({{"items", places->search(query).toVariantList()}}); }
    catch (const std::exception &e) { emit placesReady({{"error", QString::fromUtf8(e.what())}}); }
}

void GarminBackend::load(const QUrl &url) {
    if (importing) return;
    const QFileInfo file(url.toLocalFile());
    if (!url.isLocalFile() || !file.isFile() || !file.isReadable()) {
        emit prepared({{"error", tr("无法读取所选 IMG 地图。")}}); return;
    }
    QString cacheError;
    cache = ImportCache::acquire(file.canonicalFilePath(), "garmin-spatial-graph-v1", &cacheError);
    if (!cache) { emit prepared({{"error", cacheError}}); return; }
    const QString output = cache->path();
    importing = true;
    auto fail = [this](const QString &message) {
        if (importing) { importing = false; emit prepared({{"error", message}}); }
        else emit runtimeError(message);
    };
    auto finish = [this, file, output, fail](const QString &tileUrl) {
        QFile metadataFile(QDir(output).filePath("metadata.json"));
        QFile styleTemplate(":/offline/resources/garmin-style.json");
        if (!metadataFile.open(QIODevice::ReadOnly) || !styleTemplate.open(QIODevice::ReadOnly)) {
            fail(tr("无法读取佳明地图转换结果。")); return;
        }
        auto metadata = QJsonDocument::fromJson(metadataFile.readAll()).object().toVariantMap();
        if (!metadata.contains("longitude") || !metadata.contains("latitude")) { fail(tr("佳明地图没有有效范围。")); return; }
        auto style = QJsonDocument::fromJson(styleTemplate.readAll()).object();
        QJsonObject source{{"type", "vector"}, {"tiles", QJsonArray{tileUrl + "/{z}/{x}/{y}.pbf"}},
            {"minzoom", 0}, {"maxzoom", 14}, {"attribution", "Garmin IMG · libgarmin"},
            {"bounds", QJsonArray{metadata.value("west").toDouble(), metadata.value("south").toDouble(),
                                  metadata.value("east").toDouble(), metadata.value("north").toDouble()}}};
        style.insert("sources", QJsonObject{{"garmin", source}});
        QJsonArray layers;
        for (const auto &value : style.value("layers").toArray()) {
            auto layer = value.toObject();
            if (layer.value("source") == "garmin") layer.insert("source-layer", "garmin");
            layers.append(layer);
        }
        style.insert("layers", layers);
        const QString fontFolder = QDir(output).filePath("glyphs/Noto Sans Regular");
        if (!QDir().mkpath(fontFolder)) { fail(tr("无法创建本地字体目录。")); return; }
        for (const auto &range : {"0-255", "256-511", "8192-8447"}) {
            const QString name = QString(range) + ".pbf";
            if (!QFileInfo::exists(QDir(fontFolder).filePath(name)) &&
                !QFile::copy(":/offline/resources/fonts/" + name, QDir(fontFolder).filePath(name))) {
                fail(tr("无法读取离线字形资源。")); return;
            }
        }
        style.insert("glyphs", QUrl::fromLocalFile(output).toString() + "/glyphs/{fontstack}/{range}.pbf");
        // Each session has a different loopback endpoint. Never rewrite a live renderer's style.
        styleFile = std::make_unique<QTemporaryFile>(QDir::tempPath() + "/vectorune-garmin-XXXXXX.json");
        const auto bytes = QJsonDocument(style).toJson();
        if (!styleFile->open() || styleFile->write(bytes) != bytes.size() || !styleFile->flush()) {
            fail(tr("无法创建佳明地图样式。")); return;
        }
        metadata.insert("styleUrl", QUrl::fromLocalFile(styleFile->fileName()).toString());
        metadata.insert("name", file.completeBaseName());
        metadata.insert("folder", QUrl::fromLocalFile(file.absolutePath()));
        metadata.insert("zoom", 11);
        metadata.insert("attribution", "Garmin IMG · libgarmin");
        routingFile = QDir(output).filePath("routing-index.json");
        const bool routes = QFileInfo::exists(routingFile) && metadata.value("routingWays").toInt() > 0
                            && metadata.value("routingRulesComplete").toBool();
        if (!routes) routingFile.clear();
        metadata.insert("routingAvailable", routes);
        metadata.insert("capabilities", QVariantMap{{"geometry", true}, {"tiles", true}, {"search", true},
            {"nearby", true}, {"routing", routes}, {"conditionalRouting", false}});
        try { places = std::make_unique<PlaceIndex>(QDir(output).filePath("places.sqlite"), false); }
        catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); return; }
        QStringList files{"metadata.json", "geometry.sqlite", "places.sqlite"};
        if (routes) files << "routing-index.json" << "car.vgr" << "bicycle.vgr" << "walk.vgr";
        QString error;
        if (!cache->commit(files, &error)) { fail(error); return; }
        importing = false;
        emit prepared(metadata);
    };
    auto startTiles = [this, output, finish, fail] {
        auto *process = new QProcess(this);
        auto *timer = new QTimer(process); timer->setSingleShot(true);
        auto buffer = std::make_shared<QByteArray>();
        connect(timer, &QTimer::timeout, process, [process] { process->kill(); });
        connect(process, &QProcess::readyReadStandardOutput, this, [process, timer, buffer, finish] {
            buffer->append(process->readAllStandardOutput());
            int newline = buffer->indexOf('\n'); if (newline < 0) return;
            auto result = QJsonDocument::fromJson(buffer->left(newline)).object();
            if (!result.value("url").toString().startsWith("http://127.0.0.1:")) { process->kill(); return; }
            timer->stop(); buffer->clear(); finish(result.value("url").toString());
        });
        connect(process, &QProcess::readyReadStandardError, this, [process] { qWarning().noquote() << process->readAllStandardError(); });
        connect(process, &QProcess::finished, this, [fail](int, QProcess::ExitStatus) { fail(tr("地图瓦片服务退出，请重新加载地图。")); });
        connect(process, &QProcess::errorOccurred, this, [fail](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) fail(tr("无法启动地图瓦片服务。"));
        });
        process->start(RuntimePaths::helper("mapviewer_geometry_service"), {QDir(output).filePath("geometry.sqlite")});
        timer->start(15000);
    };
    auto compile = [this, output, startTiles, fail] {
        QFile file(QDir(output).filePath("metadata.json"));
        if (!file.open(QIODevice::ReadOnly)) { fail(tr("地图元数据缺失。")); return; }
        const auto metadata = QJsonDocument::fromJson(file.readAll()).object();
        if (!metadata.value("routingRulesComplete").toBool() || metadata.value("routingWays").toInt() == 0) {
            QFile::remove(QDir(output).filePath("routing.cbor")); startTiles(); return;
        }
        auto *process = new QProcess(this); auto *timer = new QTimer(process); timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, process, &QProcess::kill);
        connect(process, &QProcess::finished, this, [process, timer, output, startTiles, fail](int code, QProcess::ExitStatus status) {
            timer->stop(); process->deleteLater();
            if (code || status != QProcess::NormalExit) { fail(tr("紧凑路网索引编译失败。")); return; }
            QFile::remove(QDir(output).filePath("routing.cbor")); startTiles();
        });
        connect(process, &QProcess::errorOccurred, this, [fail](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) fail(tr("无法启动路网索引编译器。"));
        });
        process->start(RuntimePaths::helper("mapviewer_garmin_route"), {"--compile", QDir(output).filePath("routing.cbor"), output});
        timer->start(180000);
    };
    if (cache->ready()) { QTimer::singleShot(0, this, startTiles); return; }
    auto *process = new QProcess(this); auto *timer = new QTimer(process); timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, process, &QProcess::kill);
    connect(process, &QProcess::finished, this, [process, timer, compile, fail](int code, QProcess::ExitStatus status) {
        timer->stop(); const auto diagnostics = QString::fromUtf8(process->readAllStandardError()).right(1500); process->deleteLater();
        if (code || status != QProcess::NormalExit) { fail(tr("佳明地图导入失败：%1").arg(diagnostics)); return; }
        compile();
    });
    connect(process, &QProcess::errorOccurred, this, [fail](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) fail(tr("无法启动佳明地图导入程序。"));
    });
    process->start(RuntimePaths::helper("mapviewer_garmin_import"), {file.canonicalFilePath(), output, "--compact"});
    timer->start(180000);
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
