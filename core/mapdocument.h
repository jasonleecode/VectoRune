#pragma once
#include "mapregistry.h"
class MapDocument : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList fileFilters READ fileFilters CONSTANT)
public:
    explicit MapDocument(MapRegistry registry, QObject *parent = nullptr);
    Q_INVOKABLE QVariantMap prepare(const QUrl &url);
    Q_INVOKABLE void load(const QUrl &url);
    Q_INVOKABLE void searchPlaces(const QString &query);
    Q_INVOKABLE QVariantMap poiAt(const QGeoCoordinate &, double) const;
    Q_INVOKABLE void planRoute(const QGeoCoordinate &, const QGeoCoordinate &, const QString &, const QString &);
    // A renderer holds its session alive until its own destruction, even after a map switch.
    Q_INVOKABLE void retainForRenderer(QObject *renderer);
    QStringList fileFilters() const { return registry.fileFilters(); }
signals:
    void prepared(const QVariantMap &result);
    void routeReady(const QVariantMap &result);
    void placesReady(const QVariantMap &result);
private:
    void activate(const std::shared_ptr<MapBackend> &backend);
    MapRegistry registry;
    std::shared_ptr<MapBackend> active, pending;
};
