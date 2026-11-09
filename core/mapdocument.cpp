#include "mapdocument.h"
MapDocument::MapDocument(MapRegistry registry, QObject *parent)
    : QObject(parent), registry(std::move(registry)) {}
void MapDocument::activate(const std::shared_ptr<MapBackend> &backend) {
    if (active) disconnect(active.get(), nullptr, this, nullptr);
    active = backend;
    connect(active.get(), &MapBackend::routeReady, this, &MapDocument::routeReady);
    connect(active.get(), &MapBackend::placesReady, this, &MapDocument::placesReady);
}
QVariantMap MapDocument::prepare(const QUrl &url) {
    if (pending) return {{"error", tr("正在加载另一张地图。")}};
    auto candidate = registry.create(url);
    if (!candidate) return {{"error", tr("没有支持此地图格式的插件。")}};
    const auto result = candidate->prepare(url);
    if (!result.contains("error")) activate(candidate);
    return result;
}
void MapDocument::load(const QUrl &url) {
    if (pending) { emit prepared({{"error", tr("正在加载另一张地图。")}}); return; }
    pending = registry.create(url);
    if (!pending) { emit prepared({{"error", tr("没有支持此地图格式的插件。")}}); return; }
    connect(pending.get(), &MapBackend::prepared, this, [this](const QVariantMap &result) {
        auto candidate = std::move(pending);
        disconnect(candidate.get(), &MapBackend::prepared, this, nullptr);
        if (!result.contains("error")) activate(candidate);
        emit prepared(result);
    });
    // Retain through synchronous load/prepare completion as well.
    auto candidate = pending;
    candidate->load(url);
}
void MapDocument::searchPlaces(const QString &query) {
    if (active) active->searchPlaces(query);
    else emit placesReady({{"error", tr("请先加载地图。")}});
}
QVariantMap MapDocument::poiAt(const QGeoCoordinate &coordinate, double radius) const {
    return active ? active->poiAt(coordinate, radius) : QVariantMap{{"error", tr("请先加载地图。")}};
}
void MapDocument::planRoute(const QGeoCoordinate &start, const QGeoCoordinate &finish,
                            const QString &mode, const QString &strategy) {
    if (active) active->planRoute(start, finish, mode, strategy);
    else emit routeReady({{"error", tr("请先加载地图。")}});
}
void MapDocument::retainForRenderer(QObject *renderer) {
    if (renderer && active)
        connect(renderer, &QObject::destroyed, this, [session = active] {});
}
