#pragma once
#include <QGeoCoordinate>
#include <QJsonArray>
#include <QJsonObject>
QJsonObject planGarminRoute(const QJsonArray &ways, const QGeoCoordinate &start, const QGeoCoordinate &finish,
                            const QString &mode, const QString &strategy, const QJsonArray &restrictions = {},
                            bool requireDecodedAccess = false);
