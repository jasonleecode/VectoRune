#pragma once
#include <QJsonObject>
struct gmap;
struct gobject;
QJsonObject garminMetadata(gmap *map);
QJsonObject garminAttributes(gobject *object);
QJsonObject garminRoutingInfo(gobject *object);
