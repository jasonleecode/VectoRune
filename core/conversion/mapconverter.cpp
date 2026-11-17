#include "mapconverter.h"
#include "runtimepaths.h"
#include "stylepalette.h"
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <QtEndian>
#include <QtConcurrent/QtConcurrentRun>

namespace {
const QMap<QString, QStringList> groups{
    {"transportation", {"transportation", "transportation_name"}},
    {"building", {"building", "housenumber"}},
    {"water", {"water", "waterway", "waterway_detail", "water_name", "water_name_detail"}},
    {"land", {"landuse", "landcover", "park", "mountain_peak"}},
    {"poi", {"poi", "poi_detail"}}, {"place", {"place"}},
    {"boundary", {"boundary"}}, {"aeroway", {"aeroway", "aerodrome_label"}}};
bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush();
}
QByteArray readResource(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

MapConverter::MapConverter(QObject *parent) : QObject(parent) {
#ifdef VECTORUNE_WITH_TILEMAKER
    Q_INIT_RESOURCE(conversion_profiles);
#endif
    Q_INIT_RESOURCE(conversion_style);
    process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process, &QProcess::readyReadStandardOutput, this, &MapConverter::readOutput);
    connect(&process, &QProcess::finished, this, &MapConverter::finish);
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) fail(tr("无法启动转换程序：%1").arg(process.errorString()));
    });
    killTimer.setSingleShot(true);
    killTimer.setInterval(2000);
    connect(&killTimer, &QTimer::timeout, &process, &QProcess::kill);
    connect(&validation, &QFutureWatcher<QString>::finished, this, [this] {
        if (cancelled) { fail(tr("已取消，未生成导出文件。")); return; }
        const auto error = validation.result();
        if (!error.isEmpty()) { fail(error); return; }
        publishOutput();
    });
}
MapConverter::~MapConverter() {
    disconnect(&process, nullptr, this, nullptr);
    disconnect(&validation, nullptr, this, nullptr);
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(-1);
    }
    validation.waitForFinished();
}
bool MapConverter::available() const {
#ifdef VECTORUNE_WITH_TILEMAKER
    return QFileInfo(RuntimePaths::helper("tilemaker")).isExecutable();
#else
    return false;
#endif
}
QVariantList MapConverter::elements() const {
    return {QVariantMap{{"key", "transportation"}, {"label", tr("道路与铁路")}},
        QVariantMap{{"key", "building"}, {"label", tr("建筑物（含高度属性）")}},
        QVariantMap{{"key", "water"}, {"label", tr("河流与水域")}},
        QVariantMap{{"key", "land"}, {"label", tr("绿地与土地利用")}},
        QVariantMap{{"key", "poi"}, {"label", tr("兴趣点（POI）")}},
        QVariantMap{{"key", "place"}, {"label", tr("城镇与地名")}},
        QVariantMap{{"key", "boundary"}, {"label", tr("行政边界")}},
        QVariantMap{{"key", "aeroway"}, {"label", tr("机场与跑道")}}};
}
QUrl MapConverter::suggestedOutput(const QUrl &input, const QString &kind) const {
    const QFileInfo file(input.toLocalFile());
    QString base = file.fileName();
    if (base.endsWith(".osm.pbf", Qt::CaseInsensitive)) base.chop(8);
    return QUrl::fromLocalFile(file.dir().filePath(base + (kind == "xyz" ? "-tiles" : "." + kind)));
}
void MapConverter::fail(const QString &error) {
    killTimer.stop();
    busy = false;
    message = error;
    temporary.reset();
    emit changed();
}
bool MapConverter::start(const QUrl &input, const QUrl &output, const QStringList &requested,
                         const QString &style, const QString &kind, int minZoom, int maxZoom) {
    if (busy) return false;
    resultUrl = QUrl();
    outputLog.clear();
    auto reject = [this](const QString &error) { fail(error); return false; };
    if (!available()) return reject(tr("未找到内置 tilemaker 转换程序，请重新构建或安装完整版本。"));
    const QFileInfo source(input.toLocalFile()), target(output.toLocalFile());
    if (!input.isLocalFile() || !source.isFile() || !source.isReadable()
        || !source.fileName().endsWith(".osm.pbf", Qt::CaseInsensitive))
        return reject(tr("请选择可读取的 .osm.pbf 地图。"));
    if (kind != "mbtiles" && kind != "pmtiles" && kind != "xyz") return reject(tr("不支持的输出格式。"));
    if (style != "standard" && style != "light" && style != "dark") return reject(tr("不支持的显示风格。"));
    if (!output.isLocalFile() || output.toLocalFile().isEmpty() || !target.dir().exists())
        return reject(tr("请选择已存在目录中的输出路径。"));
    if (target.exists() || target.isSymLink() || QFileInfo::exists(target.absoluteFilePath() + ".style.json"))
        return reject(tr("输出路径或配套样式已存在，请选择新名称。"));
    if (kind != "xyz" && target.suffix() != kind) return reject(tr("输出文件扩展名应为 .%1。").arg(kind));
    if (minZoom < 0 || maxZoom > 14 || minZoom > maxZoom)
        return reject(tr("缩放范围须为 0–14，最小级别不能大于最大级别。"));
    selectedLayers.clear();
    for (const auto &key : requested) {
        if (!groups.contains(key)) return reject(tr("未知地图要素：%1").arg(key));
        selectedLayers.append(groups.value(key));
    }
    selectedLayers.removeDuplicates();
    if (selectedLayers.isEmpty()) return reject(tr("请至少勾选一种地图要素。"));
    destination = target.absoluteFilePath();
    format = kind;
    preset = style;
    temporary = std::make_unique<QTemporaryDir>(target.dir().filePath(".vectorune-convert-XXXXXX"));
    if (!temporary->isValid()) return reject(tr("无法在输出目录创建临时文件，请检查权限和磁盘空间。"));
    auto config = QJsonDocument::fromJson(readResource(":/conversion/config-openmaptiles.json")).object();
    auto layers = config.value("layers").toObject();
    for (const auto &key : layers.keys()) {
        auto layer = layers.value(key).toObject();
        // OSM-only profiles do not depend on external coastline/landcover files.
        if (!selectedLayers.contains(key) || layer.contains("source")) { layers.remove(key); continue; }
        layer.insert("minzoom", qMin(layer.value("minzoom").toInt(), maxZoom));
        layer.insert("maxzoom", maxZoom);
        layers.insert(key, layer);
    }
    config.insert("layers", layers);
    auto settings = config.value("settings").toObject();
    settings.insert("minzoom", minZoom);
    settings.insert("maxzoom", maxZoom);
    settings.insert("basezoom", maxZoom);
    settings.insert("name", source.completeBaseName());
    // Directory tiles can be served directly without a gzip response header.
    settings.insert("compress", kind == "xyz" ? "none" : "gzip");
    settings.insert("metadata", QJsonObject{{"attribution", "© OpenStreetMap contributors"},
        {"vectorune:style", preset}});
    settings.insert("filemetadata", QJsonObject{{"tilejson", "2.0.0"}, {"scheme", "xyz"},
        {"format", "pbf"}, {"tiles", QJsonArray{"{z}/{x}/{y}.pbf"}}, {"vectorune:style", preset}});
    config.insert("settings", settings);
    // Filter writes, including attributes, so a disabled Layer call cannot
    // accidentally attach its attributes to the previous enabled feature.
    QByteArray lua = readResource(":/conversion/process-openmaptiles.lua");
    lua += "\nlocal selected = {\n";
    for (const auto &name : layers.keys()) lua += "[\"" + name.toUtf8() + "\"] = true,\n";
    lua += R"(}
local original_init = init_function
function init_function(...)
if original_init then original_init(...) end
-- Native bindings are installed after the profile loads, before init_function.
local active = false
for _, name in ipairs({"Layer", "LayerAsCentroid"}) do
    local original = _G[name]
    _G[name] = function(layer, ...)
        active = selected[layer] == true
        if active then return original(layer, ...) end
    end
end
for _, name in ipairs({"Attribute", "AttributeNumeric", "AttributeInteger", "AttributeBoolean", "MinZoom", "ZOrder", "Score"}) do
    local original = _G[name]
    _G[name] = function(...)
        if active then return original(...) end
    end
end
end
)";
    if (layers.isEmpty() || !writeFile(temporary->filePath("config.json"), QJsonDocument(config).toJson())
        || !writeFile(temporary->filePath("process.lua"), lua))
        return reject(tr("无法准备转换配置。"));
    stagedOutput = temporary->filePath(kind == "xyz" ? "tiles" : "map." + kind);
    process.setWorkingDirectory(temporary->path());
    cancelled = false;
    busy = true;
    message = tr("正在转换地图，可继续使用 Viewer…");
    emit changed();
    process.start(RuntimePaths::helper("tilemaker"), {"--input", source.canonicalFilePath(),
        "--output", stagedOutput, "--config", temporary->filePath("config.json"),
        "--process", temporary->filePath("process.lua"), "--threads",
        QString::number(qBound(1, QThread::idealThreadCount() / 2, 8))});
    return true;
}
void MapConverter::readOutput() {
    outputLog += QString::fromUtf8(process.readAllStandardOutput()).replace('\r', '\n');
    if (outputLog.size() > 64000) outputLog = outputLog.right(64000);
    emit changed();
}
void MapConverter::cancel() {
    if (!busy || cancelled) return;
    cancelled = true;
    message = tr("正在取消…");
    if (process.state() != QProcess::NotRunning) {
        process.terminate();
        killTimer.start();
    }
    emit changed();
}
QString MapConverter::validateOutput() const {
    if (format == "mbtiles") {
        QString error;
        const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(stagedOutput);
            db.setConnectOptions("QSQLITE_OPEN_READONLY");
            if (!db.open()) error = tr("无法读取转换结果。");
            else {
                QSqlQuery q(db);
                if (!q.exec("PRAGMA quick_check") || !q.next() || q.value(0) != "ok") error = tr("输出数据库校验失败。");
                else if (!q.exec("SELECT value FROM metadata WHERE name='format'") || !q.next() || q.value(0) != "pbf")
                    error = tr("输出不是矢量 MBTiles。");
                else if (!q.exec("SELECT 1 FROM tiles LIMIT 1") || !q.next()) error = tr("所选要素和缩放范围没有生成瓦片。");
            }
        }
        QSqlDatabase::removeDatabase(connection);
        return error;
    }
    if (format == "pmtiles") {
        QFile file(stagedOutput);
        if (!file.open(QIODevice::ReadOnly)) return tr("无法读取转换结果。");
        const auto header = file.read(127);
        if (header.size() != 127 || header.left(7) != "PMTiles" || header[7] != 3
            || qFromLittleEndian<quint64>(header.constData() + 72) == 0)
            return tr("PMTiles 输出无效或没有瓦片。");
    } else {
        QDirIterator tiles(stagedOutput, {"*.pbf"}, QDir::Files, QDirIterator::Subdirectories);
        if (!tiles.hasNext() || !QFileInfo::exists(stagedOutput + "/metadata.json"))
            return tr("所选要素和缩放范围没有生成瓦片。");
    }
    return {};
}
bool MapConverter::writeStyle() {
    auto style = QJsonDocument::fromJson(readResource(":/conversion/beijing-style.json")).object();
    style = applyStylePalette(style, preset);
    auto sources = style.value("sources").toObject();
    auto source = sources.value("beijing").toObject();
    if (format == "xyz") {
        source.remove("url");
        source.insert("tiles", QJsonArray{"{z}/{x}/{y}.pbf"});
        const auto metadata = QJsonDocument::fromJson(readResource(stagedOutput + "/metadata.json")).object();
        for (const auto &key : {"minzoom", "maxzoom", "bounds"}) source.insert(key, metadata.value(key));
    } else {
        source.insert("url", (format == "mbtiles" ? "mbtiles://" : "pmtiles://")
            + QString::fromLatin1(QUrl::toPercentEncoding(destination, "/")));
    }
    sources.insert("beijing", source);
    style.insert("sources", sources);
    QJsonArray layers;
    for (const auto &entry : style.value("layers").toArray()) {
        const auto layer = entry.toObject();
        if (!layer.contains("source-layer") || selectedLayers.contains(layer.value("source-layer").toString()))
            layers.append(layer);
    }
    style.insert("layers", layers);
    return writeFile(format == "xyz" ? stagedOutput + "/style.json" : temporary->filePath("style.json"),
                     QJsonDocument(style).toJson());
}
void MapConverter::finish(int code, QProcess::ExitStatus exitStatus) {
    if (!busy) return;
    killTimer.stop();
    readOutput();
    if (cancelled) { fail(tr("已取消，未生成导出文件。")); return; }
    if (code != 0 || exitStatus != QProcess::NormalExit) {
        fail(tr("转换失败（退出码 %1），请查看运行日志。").arg(code)); return;
    }
    message = tr("正在校验导出结果…");
    emit changed();
    validation.setFuture(QtConcurrent::run([this] { return validateOutput(); }));
}
void MapConverter::publishOutput() {
    if (!writeStyle()) { fail(tr("无法写入配套样式。")); return; }
    // Stage on the destination filesystem and never overwrite an existing map.
    bool published = false;
    if (format == "xyz") published = QDir().rename(stagedOutput, destination);
    else {
        const auto stylePath = destination + ".style.json";
        if (QFile::rename(temporary->filePath("style.json"), stylePath)) {
            published = QFile::rename(stagedOutput, destination);
            if (!published) QFile::remove(stylePath);
        }
    }
    if (!published) { fail(tr("无法保存结果，输出路径可能已存在或不可写。")); return; }
    resultUrl = QUrl::fromLocalFile(destination);
    fail(tr("转换完成：%1").arg(destination));
}
