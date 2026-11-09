#include "router.h"
#include <QCoreApplication>
#include <cstdio>
#include <QTemporaryDir>
#include <QFile>

static QJsonObject way(QJsonArray coordinates, bool oneway = false, QJsonArray joins = {0, 1, 2},
                       int type = 6) {
    return {{"coordinates", coordinates}, {"junctions", joins}, {"hasJunctionFlags", true},
            {"oneway", oneway},           {"type", type},       {"name", "test"}};
}
static void check(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
static QJsonObject roundTripRoute(const QJsonArray &ways, const QGeoCoordinate &start, const QGeoCoordinate &finish,
                                  const QString &mode, const QString &strategy, const QJsonArray &restrictions = {},
                                  bool requireDecodedAccess = false) {
    RoutingGraph original(ways, restrictions, mode, requireDecodedAccess);
    auto expected = original.route(start, finish, strategy);
    QTemporaryDir folder; QString error; const auto path = folder.filePath("graph.vgr");
    check(original.save(path, &error), qPrintable(error));
    auto mapped = RoutingGraph::load(path, &error);
    check(bool(mapped), qPrintable(error));
    auto result = mapped->route(start, finish, strategy);
    check(result == expected, "mapped graph must preserve all route data and errors");
    mapped.reset();
    QFile file(path); check(file.open(QIODevice::ReadWrite), "open compiled fixture");
    file.resize(file.size() - 1); file.close();
    check(!RoutingGraph::load(path, &error), "truncated graph must be rejected");
    return result;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // Spatial candidates must agree with the reference full scan across the wrap,
    // near the poles and on a grid boundary (including equidistant candidates).
    for (const auto &origin : {QGeoCoordinate(0, 179.999), QGeoCoordinate(89.999, 45),
                              QGeoCoordinate(40, 116)}) {
        const auto end = origin.atDistanceAndAzimuth(350, 90);
        auto segment = way({QJsonArray{origin.longitude(), origin.latitude()},
                            QJsonArray{end.longitude(), end.latitude()}}, false, {0, 1});
        auto snapped = roundTripRoute({segment}, origin.atDistanceAndAzimuth(100, 0), end,
                                      "car", "shortest");
        check(!snapped.contains("error"), "spatial snap across wrap, pole and grid boundary");
    }
    QGeoCoordinate a(40, 116), b(40, 116.002), c(39.999, 116.001), d(40.001, 116.001);
    QJsonArray horizontal{QJsonArray{116, 40}, QJsonArray{116.001, 40}, QJsonArray{116.002, 40}};
    auto result = roundTripRoute({way(horizontal, true)}, b, a, "car", "shortest");
    check(result.contains("error"), "car must respect one-way direction");
    result = roundTripRoute({way(horizontal, true)}, b, a, "walk", "shortest");
    check(!result.contains("error"), "walking can traverse opposite to a road one-way flag");
    QJsonArray vertical{QJsonArray{116.001, 39.999}, QJsonArray{116.001, 40}, QJsonArray{116.001, 40.001}};
    result = roundTripRoute({way(horizontal, false, {0, 2}), way(vertical, false, {0, 2})}, a, d, "car",
                             "shortest");
    check(result.contains("error"), "geometric bridge crossing is not a routing junction");
    result = roundTripRoute({way(horizontal), way(vertical)}, a, d, "car", "shortest");
    check(!result.contains("error"), "marked routing junction must connect roads");
    check(result.value("distance").toDouble() > 100, "route distance must follow road geometry");
    check(result.value("instructions").toArray().last().toObject().value("maneuver") == "arrive",
          "arrival instruction");
    check(result.value("cumulative").toArray().size() == result.value("coordinates").toArray().size(),
          "progress and geometry lengths");
    result = roundTripRoute({way(horizontal, false, {0, 1, 2}, 0x16)}, a, b, "car", "shortest");
    check(result.contains("error"), "car must not use a trail");
    result = roundTripRoute({way(horizontal)}, QGeoCoordinate(45, 116), b, "car", "shortest");
    check(result.contains("error"), "far-away start must not silently snap");
    auto ab = way({QJsonArray{116, 40}, QJsonArray{116.001, 40}});
    auto bd = way({QJsonArray{116.001, 40}, QJsonArray{116.001, 40.001}});
    auto ad = way({QJsonArray{116, 40}, QJsonArray{116, 40.002}, QJsonArray{116.001, 40.001}});
    ab.insert("roadId", "ab");
    bd.insert("roadId", "bd");
    ad.insert("roadId", "ad");
    QJsonObject ban{
        {"nodes", QJsonArray{QJsonArray{116, 40}, QJsonArray{116.001, 40}, QJsonArray{116.001, 40.001}}},
        {"roads", QJsonArray{"ab", "bd"}},
        {"exceptMask", 0x10}};
    auto unrestricted = roundTripRoute({ab, bd, ad}, a, d, "car", "shortest");
    result = roundTripRoute({ab, bd, ad}, a, d, "car", "shortest", {ban});
    check(!result.contains("error") &&
              result["distance"].toDouble() > unrestricted["distance"].toDouble() + 50,
          "turn ban must force a detour, including alternative incoming state");
    result = roundTripRoute({ab, bd}, a, d, "car", "shortest", {ban});
    check(result.contains("error"), "cannot take the forbidden turn when no detour exists");
    result = roundTripRoute({ab, ab, bd}, a, d, "car", "shortest", {ban});
    check(result.contains("error"), "duplicate geometry cannot bypass a road restriction");
    result = roundTripRoute({ab, bd}, a, d, "walk", "shortest", {ban});
    check(!result.contains("error"), "pedestrian exception must be honored");
    ab.insert("accessDecoded", true);
    ab.insert("accessMask", 1);
    result = roundTripRoute({ab}, a, QGeoCoordinate(40, 116.001), "car", "shortest");
    check(result.contains("error"), "decoded no-car rule overrides road type");
    result = roundTripRoute({ab}, a, QGeoCoordinate(40, 116.001), "bicycle", "shortest");
    check(!result.contains("error"), "no-car does not prohibit bicycle");
    ab.insert("accessMask", 0x20);
    result = roundTripRoute({ab}, a, QGeoCoordinate(40, 116.001), "bicycle", "shortest");
    check(result.contains("error"), "decoded no-bicycle rule");
    ab.insert("accessMask", 0x10);
    result = roundTripRoute({ab}, a, QGeoCoordinate(40, 116.001), "walk", "shortest");
    check(result.contains("error"), "decoded no-foot rule");
    ab.insert("accessMask", 8);
    result = roundTripRoute({ab}, a, QGeoCoordinate(40, 116.001), "car", "shortest");
    check(result.contains("error"), "ordinary car does not assume carpool eligibility");
    ab.insert("accessMask", 0);
    bd.insert("accessDecoded", true);
    bd.insert("accessMask", 0x80);
    auto de = way({QJsonArray{116.001, 40.001}, QJsonArray{116.002, 40.001}});
    QGeoCoordinate e(40.001, 116.002);
    result = roundTripRoute({ab, bd, de}, a, e, "car", "shortest");
    check(result.contains("error"), "no-through road must not connect two ordinary roads as a shortcut");
    result = roundTripRoute({ab, bd}, a, d, "car", "shortest");
    check(!result.contains("error"), "no-through road permits destination access");
    result = roundTripRoute({ab, bd}, d, a, "car", "shortest");
    check(!result.contains("error"), "no-through road permits origin access");
    bd.insert("accessMask", 0);
    de.insert("roadId", "de");
    ab.insert("oneway", true);
    bd.insert("oneway", true);
    de.insert("oneway", true);
    QJsonObject viaBan{{"nodes", QJsonArray{QJsonArray{116, 40}, QJsonArray{116.001, 40},
                                            QJsonArray{116.001, 40.001}, QJsonArray{116.002, 40.001}}},
                       {"roads", QJsonArray{"ab", "bd", "de"}}};
    result = roundTripRoute({ab, bd, de}, a, e, "car", "shortest", {viaBan});
    check(result.contains("error"), "multi-via restriction must block the full forbidden sequence");
    result = roundTripRoute({ab, bd, de, ad}, a, e, "car", "shortest", {viaBan});
    check(!result.contains("error"), "multi-via restriction permits a different incoming path");
    result = roundTripRoute({way(horizontal)}, a, b, "car", "shortest", {}, true);
    check(result.contains("error"), "strict routing cannot silently use undecoded access");
    puts("PASS: road access, carpool, endpoint-only access, turn bans/exceptions, multi-via, duplicate "
         "geometry, one-way, bridges, snapping");
    return 0;
}
