#pragma once
#include "mapbackend.h"
#include "importcache.h"
#include "placeindex.h"
#include "routing/routeservice.h"
#include <memory>
class GarminBackend final : public MapBackend {
public:
    explicit GarminBackend(QObject *parent = nullptr);
    ~GarminBackend() override;
    void load(const QUrl &url) override;
    void searchPlaces(const QString &query) override;
    QVariantMap poiAt(const QGeoCoordinate &, double) const override;
    void planRoute(const QGeoCoordinate &, const QGeoCoordinate &, const QString &, const QString &) override;
private:
    std::shared_ptr<ImportCache> cache;
    bool importing = false;
    std::unique_ptr<PlaceIndex> places;
    QString routingFile;
    RouteService *routeService = nullptr;
};
