#include "backend.h"
#include "runtimepaths.h"

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

MbtilesBackend::MbtilesBackend(QObject *parent) : MapBackend(parent) { Q_INIT_RESOURCE(mbtiles_style); }

static QString searchKey(const QString &text) {
    QString result;
    for (auto c : text.normalized(QString::NormalizationForm_D).toCaseFolded())
        if (c.category() != QChar::Mark_NonSpacing) result += c;
    return result.trimmed();
}

void MbtilesBackend::searchPlaces(const QString &query)
{
    if (searchMapFile.isEmpty()) { MapBackend::searchPlaces(query); return; }
    const auto generation = ++searchGeneration;
    if (searchProcess && searchProcess->state() != QProcess::NotRunning) {
        searchProcess->kill();
        searchProcess->waitForFinished(1000);
    }
    const auto term = searchKey(query);
    if (term.isEmpty()) { emit placesReady({{"items", QVariantList{}}}); return; }
    auto *process = new QProcess(this);
    searchProcess = process;
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, process, [process] { process->kill(); });
    auto complete = [this, process, generation](bool ok) {
        const auto result = QJsonDocument::fromJson(process->readAllStandardOutput()).object().toVariantMap();
        if (searchProcess == process) searchProcess = nullptr;
        process->deleteLater();
        if (generation != searchGeneration) return;
        emit placesReady(result.contains("items") || result.contains("error") ? result
            : QVariantMap{{"error", ok ? tr("搜索结果无效。") : tr("地图搜索失败或超时。")}});
    };
    connect(process, &QProcess::finished, this, [complete](int code, QProcess::ExitStatus status) { complete(code == 0 && status == QProcess::NormalExit); });
    connect(process, &QProcess::errorOccurred, this, [complete](QProcess::ProcessError error) { if (error == QProcess::FailedToStart) complete(false); });
    process->start(RuntimePaths::helper("mapviewer_place_search"), {searchMapFile, query});
    timer->start(180000);
}

QVariantMap MbtilesBackend::prepare(const QUrl &url)
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
    searchMapFile = format == "pbf" ? file.canonicalFilePath() : QString();
    ++searchGeneration;
    return {{"capabilities", QVariantMap{{"tiles", true}, {"geometry", false}, {"search", format == "pbf"},
                {"nearby", false}, {"routing", false}, {"conditionalRouting", false}}}, {"styleUrl", styleUrl}, {"name", file.completeBaseName()},
            {"attribution", metadata.value("attribution", "© OpenStreetMap contributors")},
            {"folder", QUrl::fromLocalFile(file.absolutePath())},
            {"latitude", latitude}, {"longitude", longitude}, {"zoom", zoom}};
}
