#include "routeservice.h"
#include "runtimepaths.h"
#include <QGeoRectangle>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
RouteService::RouteService(QObject *parent) : QObject(parent) {
    timeout.setSingleShot(true); idle.setSingleShot(true);
    connect(&process, &QProcess::started, this, [this] { process.write(request); });
    connect(&process, &QProcess::readyReadStandardOutput, this, &RouteService::receive);
    connect(&process, &QProcess::readyReadStandardError, this, [this] { process.readAllStandardError(); });
    connect(&process, &QProcess::finished, this, [this] {
        receive();
        if (busy) fail(tr("路线规划进程意外退出。"));
        currentFile.clear();
    });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) fail(tr("无法启动路线规划程序。"));
    });
    connect(&timeout, &QTimer::timeout, this, [this] { fail(tr("路线规划超时。")); process.kill(); });
    connect(&idle, &QTimer::timeout, this, [this] { process.closeWriteChannel(); currentFile.clear(); });
}
RouteService::~RouteService() {
    process.disconnect();
    if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(1000); }
}
void RouteService::plan(const QString &file, const QGeoCoordinate &start, const QGeoCoordinate &finish,
                        const QString &mode, const QString &strategy) {
    if (busy) return;
    if (!start.isValid() || !finish.isValid()) { emit ready({{"error", tr("起终点经纬度无效。")}}); return; }
    idle.stop(); buffer.clear();
    QJsonObject value{{"start", QJsonArray{start.longitude(), start.latitude()}},
                      {"finish", QJsonArray{finish.longitude(), finish.latitude()}},
                      {"mode", mode}, {"strategy", strategy}};
    request = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
    if (process.state() != QProcess::NotRunning && (currentFile != file || !process.isWritable())) {
        process.kill(); process.waitForFinished(1000);
    }
    busy = true;
    if (process.state() == QProcess::NotRunning) {
        currentFile = file;
        process.start(RuntimePaths::helper("mapviewer_garmin_route"), {"--serve", file});
    } else process.write(request);
    timeout.start(60000);
}
void RouteService::fail(const QString &message) {
    if (!busy) return;
    busy = false; timeout.stop();
    emit ready({{"error", message}});
}
void RouteService::receive() {
    buffer += process.readAllStandardOutput();
    if (!busy) { buffer.clear(); return; }
    if (buffer.size() > 32 * 1024 * 1024) { fail(tr("路线结果过大。")); process.kill(); return; }
    const int newline = buffer.indexOf('\n');
    if (newline < 0) return;
    const auto json = QJsonDocument::fromJson(buffer.left(newline)).object();
    buffer.remove(0, newline + 1);
    if (json.contains("error")) { fail(json.value("error").toString()); idle.start(60000); return; }
    const auto coordinates = json.value("coordinates").toArray();
    if (coordinates.size() < 2) { fail(tr("路线规划返回了无效数据。")); return; }
    QVariantList path;
    QList<QGeoCoordinate> points;
    for (const auto &value : coordinates) {
        const auto pair = value.toArray();
        if (pair.size() != 2 || !pair[0].isDouble() || !pair[1].isDouble()) {
            fail(tr("路线坐标损坏。")); return;
        }
        const QGeoCoordinate point(pair[1].toDouble(), pair[0].toDouble());
        if (!point.isValid()) { fail(tr("路线坐标无效。")); return; }
        path.append(QVariant::fromValue(point)); points.append(point);
    }
    auto result = json.toVariantMap();
    result.insert("path", path);
    QGeoRectangle bounds(points);
    bounds.setWidth(qMax(0.001, bounds.width() * 1.15));
    bounds.setHeight(qMax(0.001, bounds.height() * 1.15));
    result.insert("bounds", QVariant::fromValue(bounds));
    busy = false; timeout.stop(); idle.start(60000);
    emit ready(result);
}
