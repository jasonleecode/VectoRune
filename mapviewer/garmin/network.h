#pragma once
#include <QByteArray>
#include <QJsonObject>
QJsonObject decodeGarminNodRules(const QByteArray &data, const QString &mapId, int align);
struct gmap;
struct gobject;
QJsonObject garminNetwork(gmap *map);
QJsonObject garminRoadRules(gobject *object);
