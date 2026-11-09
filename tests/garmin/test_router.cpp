#include "router.h"
#include <QCoreApplication>
#include <cstdio>

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
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QGeoCoordinate a(40, 116), b(40, 116.002), c(39.999, 116.001), d(40.001, 116.001);
    QJsonArray horizontal{QJsonArray{116, 40}, QJsonArray{116.001, 40}, QJsonArray{116.002, 40}};
    auto result = planGarminRoute({way(horizontal, true)}, b, a, "car", "shortest");
    check(result.contains("error"), "car must respect one-way direction");
    result = planGarminRoute({way(horizontal, true)}, b, a, "walk", "shortest");
    check(!result.contains("error"), "walking can traverse opposite to a road one-way flag");
    QJsonArray vertical{QJsonArray{116.001, 39.999}, QJsonArray{116.001, 40}, QJsonArray{116.001, 40.001}};
    result = planGarminRoute({way(horizontal, false, {0, 2}), way(vertical, false, {0, 2})}, a, d, "car",
                             "shortest");
    check(result.contains("error"), "geometric bridge crossing is not a routing junction");
    result = planGarminRoute({way(horizontal), way(vertical)}, a, d, "car", "shortest");
    check(!result.contains("error"), "marked routing junction must connect roads");
    check(result.value("distance").toDouble() > 100, "route distance must follow road geometry");
    check(result.value("instructions").toArray().last().toObject().value("maneuver") == "arrive",
          "arrival instruction");
    check(result.value("cumulative").toArray().size() == result.value("coordinates").toArray().size(),
          "progress and geometry lengths");
    result = planGarminRoute({way(horizontal, false, {0, 1, 2}, 0x16)}, a, b, "car", "shortest");
    check(result.contains("error"), "car must not use a trail");
    result = planGarminRoute({way(horizontal)}, QGeoCoordinate(45, 116), b, "car", "shortest");
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
    auto unrestricted = planGarminRoute({ab, bd, ad}, a, d, "car", "shortest");
    result = planGarminRoute({ab, bd, ad}, a, d, "car", "shortest", {ban});
    check(!result.contains("error") &&
              result["distance"].toDouble() > unrestricted["distance"].toDouble() + 50,
          "turn ban must force a detour, including alternative incoming state");
    result = planGarminRoute({ab, bd}, a, d, "car", "shortest", {ban});
    check(result.contains("error"), "cannot take the forbidden turn when no detour exists");
    result = planGarminRoute({ab, ab, bd}, a, d, "car", "shortest", {ban});
    check(result.contains("error"), "duplicate geometry cannot bypass a road restriction");
    result = planGarminRoute({ab, bd}, a, d, "walk", "shortest", {ban});
    check(!result.contains("error"), "pedestrian exception must be honored");
    ab.insert("accessDecoded", true);
    ab.insert("accessMask", 1);
    result = planGarminRoute({ab}, a, QGeoCoordinate(40, 116.001), "car", "shortest");
    check(result.contains("error"), "decoded no-car rule overrides road type");
    result = planGarminRoute({ab}, a, QGeoCoordinate(40, 116.001), "bicycle", "shortest");
    check(!result.contains("error"), "no-car does not prohibit bicycle");
    ab.insert("accessMask", 0x20);
    result = planGarminRoute({ab}, a, QGeoCoordinate(40, 116.001), "bicycle", "shortest");
    check(result.contains("error"), "decoded no-bicycle rule");
    ab.insert("accessMask", 0x10);
    result = planGarminRoute({ab}, a, QGeoCoordinate(40, 116.001), "walk", "shortest");
    check(result.contains("error"), "decoded no-foot rule");
    ab.insert("accessMask", 8);
    result = planGarminRoute({ab}, a, QGeoCoordinate(40, 116.001), "car", "shortest");
    check(result.contains("error"), "ordinary car does not assume carpool eligibility");
    ab.insert("accessMask", 0);
    bd.insert("accessDecoded", true);
    bd.insert("accessMask", 0x80);
    auto de = way({QJsonArray{116.001, 40.001}, QJsonArray{116.002, 40.001}});
    QGeoCoordinate e(40.001, 116.002);
    result = planGarminRoute({ab, bd, de}, a, e, "car", "shortest");
    check(result.contains("error"), "no-through road must not connect two ordinary roads as a shortcut");
    result = planGarminRoute({ab, bd}, a, d, "car", "shortest");
    check(!result.contains("error"), "no-through road permits destination access");
    result = planGarminRoute({ab, bd}, d, a, "car", "shortest");
    check(!result.contains("error"), "no-through road permits origin access");
    bd.insert("accessMask", 0);
    de.insert("roadId", "de");
    ab.insert("oneway", true);
    bd.insert("oneway", true);
    de.insert("oneway", true);
    QJsonObject viaBan{{"nodes", QJsonArray{QJsonArray{116, 40}, QJsonArray{116.001, 40},
                                            QJsonArray{116.001, 40.001}, QJsonArray{116.002, 40.001}}},
                       {"roads", QJsonArray{"ab", "bd", "de"}}};
    result = planGarminRoute({ab, bd, de}, a, e, "car", "shortest", {viaBan});
    check(result.contains("error"), "multi-via restriction must block the full forbidden sequence");
    result = planGarminRoute({ab, bd, de, ad}, a, e, "car", "shortest", {viaBan});
    check(!result.contains("error"), "multi-via restriction permits a different incoming path");
    result = planGarminRoute({way(horizontal)}, a, b, "car", "shortest", {}, true);
    check(result.contains("error"), "strict routing cannot silently use undecoded access");
    puts("PASS: road access, carpool, endpoint-only access, turn bans/exceptions, multi-via, duplicate "
         "geometry, one-way, bridges, snapping");
    return 0;
}
