#pragma once
#include <QObject>
#include <QGeoCoordinate>
#include <QUrl>
#include <QVariantMap>

// A format session owns data and worker processes, never a window or renderer.
// Capabilities describe the opened dataset, not just the file extension.
class MapBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~MapBackend() override;
    virtual QVariantMap prepare(const QUrl &url);
    virtual void load(const QUrl &url);
    virtual void searchPlaces(const QString &query);
    virtual QVariantMap poiAt(const QGeoCoordinate &, double) const;
    virtual void planRoute(const QGeoCoordinate &, const QGeoCoordinate &,
                           const QString &, const QString &);
signals:
    void runtimeError(const QString &message);
    void prepared(const QVariantMap &result);
    void routeReady(const QVariantMap &result);
    void placesReady(const QVariantMap &result);
protected:
    void stopWorkers();
};
