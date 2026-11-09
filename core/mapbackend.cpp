#include "mapbackend.h"
#include <QProcess>

MapBackend::~MapBackend() { stopWorkers(); }
void MapBackend::stopWorkers() {
    for (auto *process : findChildren<QProcess *>()) {
        process->disconnect();
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
}
QVariantMap MapBackend::prepare(const QUrl &) {
    return {{"error", tr("此地图需要异步加载。")}};
}
void MapBackend::load(const QUrl &url) { emit prepared(prepare(url)); }
void MapBackend::searchPlaces(const QString &) {
    emit placesReady({{"error", tr("当前地图不支持地名搜索。")}});
}
QVariantMap MapBackend::poiAt(const QGeoCoordinate &, double) const {
    return {{"error", tr("当前地图不支持地点详情。")}};
}
void MapBackend::planRoute(const QGeoCoordinate &, const QGeoCoordinate &, const QString &, const QString &) {
    emit routeReady({{"error", tr("当前地图不支持路线规划。")}});
}
