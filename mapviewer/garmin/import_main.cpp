#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "libgarmin.h"
#include "attributes.h"
#include "network.h"

static int parserWarnings = 0;
static void parserLog(char *, int, int level, char *format, ...)
{
    if (level > 1) return;
    va_list args;
    va_start(args, format);
    if (strstr(format, "Invalid") || strstr(format, "Error") || strstr(format, "Failed")) {
        if (parserWarnings++ < 12) vfprintf(stderr, format, args);
    }
    va_end(args);
}

static QString category(int group, int type)
{
    if (group == GO_POLYGON) {
        if (type >= 0x28 && type <= 0x4d) return "water";
        if ((type >= 0x14 && type <= 0x20) || (type >= 0x4e && type <= 0x52)) return "green";
        return "area";
    }
    if (group == GO_POINT) return "poi";
    if (type >= 0x20 && type <= 0x25) return "contour";
    if (type == 0x14) return "rail";
    if (type == 0x15 || type == 0x18 || type == 0x1f || type == 0x26) return "waterway";
    if (type >= 0x1c && type <= 0x1e) return "boundary";
    if (type >= 1 && type <= 5) return "major";
    if (type == 0x16) return "trail";
    return "road";
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 3) {
        fprintf(stderr, "Usage: mapviewer_garmin_import input.img output-directory\n");
        return 1;
    }
    gar_config config{};
    config.opm = OPM_GPS;
    config.debuglevel = 1;
    auto *garmin = gar_init_cfg(nullptr, parserLog, &config);
    if (!garmin || gar_img_load(garmin, argv[1], 1) <= 0) {
        fprintf(stderr, "libgarmin could not read this IMG (unsupported, locked, or invalid format).\n");
        return 2;
    }
    auto *map = gar_find_subfiles(garmin, nullptr, 0);
    if (!map || map->lastsub == 0) {
        fprintf(stderr, "No readable Garmin submaps.\n");
        return 2;
    }
    gobject *objects = nullptr;
    // Request the finest available resolution once, rather than duplicating
    // geometry from all Garmin display levels.
    int bits = std::min(24, map->basebits + map->zoomlevels);
    int objectCount = gar_get_objects(map, bits, nullptr, &objects, 0);
    if (objectCount <= 0 || !objects) {
        fprintf(stderr, "No drawable geometry at Garmin resolution %d.\n", bits);
        return 2;
    }
    auto sourceMetadata = garminMetadata(map);
    auto network = garminNetwork(map);
    QDir output(QString::fromLocal8Bit(argv[2]));
    if (!output.mkpath(".")) return 3;
    QFile geojson(output.filePath("map.geojson"));
    if (!geojson.open(QIODevice::WriteOnly)) return 3;
    geojson.write("{\"type\":\"FeatureCollection\",\"features\":[");
    int points = 0, lines = 0, polygons = 0, skipped = 0;
    QJsonArray poiIndex;
    QJsonArray routingWays;
    QJsonObject namedCounts;
    double west = 180, east = -180, south = 90, north = -90;
    for (auto *object = objects; object; object = object->next) {
        const int group = object->type;
        if (group != GO_POINT && group != GO_POLYLINE && group != GO_POLYGON) continue;
        gcoord coordinate{};
        if (!gar_get_object_coord(map, object, &coordinate)) { ++skipped; continue; }
        QJsonArray path;
        bool valid = true;
        auto append = [&] {
            // The public API gives signed Garmin semicircles (2^24 per turn).
            const double lon = coordinate.x * (360.0 / 16777216.0);
            const double lat = coordinate.y * (360.0 / 16777216.0);
            if (!std::isfinite(lon) || !std::isfinite(lat) || std::abs(lon) > 180 || std::abs(lat) > 85.051129) {
                valid = false;
                return;
            }
            path.append(QJsonArray{lon, lat});
        };
        append();
        if (group != GO_POINT) {
            gcoord delta{};
            for (int i = 0; gar_get_object_dcoord(map, object, i, &delta); ++i) {
                coordinate.x += delta.x;
                coordinate.y += delta.y;
                append();
            }
        }
        if (!valid || (group == GO_POLYGON && path.size() < 3)
            || (group == GO_POLYLINE && path.size() < 2)) { ++skipped; continue; }
        for (const auto &point : path) {
            const auto p = point.toArray();
            west = std::min(west, p[0].toDouble()); east = std::max(east, p[0].toDouble());
            south = std::min(south, p[1].toDouble()); north = std::max(north, p[1].toDouble());
        }
        QJsonObject geometry;
        if (group == GO_POINT) {
            geometry = {{"type", "Point"}, {"coordinates", path.first()}};
            ++points;
        } else if (group == GO_POLYLINE) {
            geometry = {{"type", "LineString"}, {"coordinates", path}};
            ++lines;
        } else {
            if (path.first() != path.last()) path.append(path.first());
            geometry = {{"type", "Polygon"}, {"coordinates", QJsonArray{path}}};
            ++polygons;
        }
        const int type = gar_obj_type(object);
        auto properties = garminAttributes(object);
        properties.insert("class", category(group, type));
        QJsonObject feature{{"type", "Feature"}, {"geometry", geometry}, {"properties", properties}};
        if (properties.contains("name")) {
            const QString key = group == GO_POINT ? "namedPoints" : group == GO_POLYLINE ? "namedLines" : "namedPolygons";
            namedCounts[key] = namedCounts.value(key).toInt() + 1;
        }
        if (group == GO_POINT) {
            properties.insert("longitude", path.first().toArray()[0]);
            properties.insert("latitude", path.first().toArray()[1]);
            poiIndex.append(properties);
        } else if (group == GO_POLYLINE) {
            auto routing = garminRoutingInfo(object);
            if (!routing.isEmpty()) {
                const auto rules = garminRoadRules(object);
                for (auto i=rules.begin(); i!=rules.end(); ++i) routing.insert(i.key(),i.value());
                routing.insert("coordinates", path);
                routing.insert("name", properties.value("name"));
                routing.insert("type", type);
                if (!routing.contains("oneway")) routing.insert("oneway", properties.value("oneway"));
                routingWays.append(routing);
            }
        }
        if (points + lines + polygons > 1) geojson.write(",");
        const auto bytes = QJsonDocument(feature).toJson(QJsonDocument::Compact);
        if (geojson.write(bytes) != bytes.size()) return 3;
    }
    geojson.write("]}");
    if (!geojson.flush() || points + lines + polygons == 0) return 3;
    QJsonObject metadata{{"points", points}, {"lines", lines}, {"polygons", polygons},
        {"skipped", skipped}, {"parserWarnings", parserWarnings}, {"resolutionBits", bits},
        {"west", west}, {"east", east}, {"south", south}, {"north", north},
        {"longitude", (west + east) / 2}, {"latitude", (south + north) / 2}};
    metadata.insert("sourceMetadata", sourceMetadata);
    metadata.insert("names", namedCounts);
    QFile poiFile(output.filePath("pois.json"));
    if (!poiFile.open(QIODevice::WriteOnly)) return 3;
    const auto poiBytes = QJsonDocument(poiIndex).toJson(QJsonDocument::Compact);
    if (poiFile.write(poiBytes) != poiBytes.size() || !poiFile.flush()) return 3;
    QFile routeFile(output.filePath("routing.json"));
    if (!routeFile.open(QIODevice::WriteOnly)) return 3;
    network.insert("ways",routingWays);
    const auto routeBytes = QJsonDocument(network).toJson(QJsonDocument::Compact);
    if (routeFile.write(routeBytes) != routeBytes.size() || !routeFile.flush()) return 3;
    metadata.insert("routingWays", routingWays.size());
    metadata.insert("routingRules",network.value("reports"));
    metadata.insert("routingRulesComplete",network.value("complete"));
    QFile meta(output.filePath("metadata.json"));
    if (!meta.open(QIODevice::WriteOnly)) return 3;
    const auto bytes = QJsonDocument(metadata).toJson();
    if (meta.write(bytes) != bytes.size() || !meta.flush()) return 3;
    QTextStream(stdout) << bytes;
    // This old parser has incomplete cleanup APIs. The isolated worker exits
    // after each import, releasing all parser state and file descriptors.
    return 0;
}
