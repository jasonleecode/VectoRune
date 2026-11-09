#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantMap>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QGeoCoordinate>
#include <memory>
#include <vector>

class MapDocument : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QVariantMap prepare(const QUrl &url);
    Q_INVOKABLE void load(const QUrl &url);
    Q_INVOKABLE QVariantMap poiAt(const QGeoCoordinate &coordinate, double radiusMeters) const;
    Q_INVOKABLE void planRoute(const QGeoCoordinate &start, const QGeoCoordinate &finish,
                              const QString &mode, const QString &strategy);
signals:
    void prepared(const QVariantMap &result);
    void routeReady(const QVariantMap &result);
private:
    // Keep previous styles alive until their old rendering objects are gone.
    std::vector<std::unique_ptr<QTemporaryFile>> styles;
    std::vector<std::unique_ptr<QTemporaryDir>> garminMaps;
    bool importing = false;
    QJsonArray poiIndex;
    QString routingFile;
    bool routing = false;
};
