#include "geometryindex.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <cmath>
#include <cstdio>
static QJsonArray coordinate(double x, double y) {
    double worldX = (8192 + x / 4096) / 16384, worldY = (8192 + y / 4096) / 16384;
    return {worldX * 360 - 180, std::atan(std::sinh(M_PI * (1 - 2 * worldY))) * 180 / M_PI};
}
static QJsonArray ring(std::initializer_list<QPair<double, double>> points) {
    QJsonArray out;
    for (auto p : points)
        out.append(coordinate(p.first, p.second));
    out.append(out.first());
    return out;
}
static QJsonObject feature(const char *type, const QJsonArray &coordinates, const char *name,
                           const char *category) {
    return {{"type", "Feature"},
            {"geometry", QJsonObject{{"type", type}, {"coordinates", coordinates}}},
            {"properties", QJsonObject{{"name", name}, {"class", category}}}};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2)
        return 1;
    try {
        QDir directory(QString::fromLocal8Bit(argv[1]));
        directory.mkpath(".");
        {
            GeometryIndex index(directory.filePath("geometry.sqlite"), true);
            index.add(feature("Point", coordinate(300, 300), "point", "poi"));
            index.add(feature("LineString", {coordinate(-300, 500), coordinate(4500, 500)}, "line", "major"));
            index.add(feature("Polygon",
                              {ring({{500, 500}, {3500, 500}, {3500, 3500}, {500, 3500}}),
                               ring({{1000, 1000}, {1000, 2000}, {2000, 2000}, {2000, 1000}})},
                              "hole", "area"));
            index.add(feature("Polygon",
                              QJsonArray{QJsonValue(ring({{-1000, 5000},
                                                          {-1000, 1000},
                                                          {1000, 1000},
                                                          {1000, 4500},
                                                          {3000, 4500},
                                                          {3000, 1000},
                                                          {5000, 1000},
                                                          {5000, 5000}}))},
                              "split", "water"));
            index.add(feature("Point", {116, 40}, "far", "poi"));
            index.add(
                feature("LineString", {QJsonArray{179.99, 0}, QJsonArray{-179.99, 0}}, "dateline", "major"));
            index.commit();
        }
        GeometryIndex index(directory.filePath("geometry.sqlite"), false);
        int read = 0;
        auto bytes = index.tile(14, 8192, 8192, &read);
        if (read != 4)
            throw std::runtime_error("spatial query read outside features");
        QFile tile(directory.filePath("tile.pbf"));
        if (!tile.open(QIODevice::WriteOnly) || tile.write(bytes) != bytes.size())
            return 2;
        int empty = 0;
        index.tile(14, 0, 0, &empty);
        if (empty)
            throw std::runtime_error("empty tile read features");
        bool rejected = false;
        try {
            index.tile(15, 0, 0);
        } catch (...) {
            rejected = true;
        }
        if (!rejected)
            return 2;
        for (int x : {0, 16383}) {
            int count = 0;
            index.tile(14, x, 8192, &count);
            if (count != 1)
                throw std::runtime_error("dateline split missing");
        }
        puts("PASS: bounded spatial query, empty tile, invalid coordinates, antimeridian coverage");
        return 0;
    } catch (const std::exception &e) {
        fprintf(stderr, "%s\n", e.what());
        return 2;
    }
}
