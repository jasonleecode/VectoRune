#pragma once
#include <QGeoCoordinate>
#include <QJsonArray>
#include <QJsonObject>
QJsonObject planGarminRoute(const QJsonArray &ways, const QGeoCoordinate &start, const QGeoCoordinate &finish,
                            const QString &mode, const QString &strategy, const QJsonArray &restrictions = {},
                            bool requireDecodedAccess = false);

#include <memory>
// Immutable graph plus turn automaton; request state is allocated separately.
class RoutingGraph {
public:
    RoutingGraph(const QJsonArray &ways, const QJsonArray &restrictions, const QString &mode,
                 bool requireDecodedAccess = true);
    ~RoutingGraph();
    bool save(const QString &path, QString *error) const;
    static std::unique_ptr<RoutingGraph> load(const QString &path, QString *error);
    QString travelMode() const;
    QJsonObject route(const QGeoCoordinate &start, const QGeoCoordinate &finish, const QString &strategy) const;
private:
    RoutingGraph();
    struct Impl;
    std::unique_ptr<Impl> impl;
};
