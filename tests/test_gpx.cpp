#include "gpxtrack.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <cstdio>

static void check(bool ok, const char *message) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static QJsonArray features(const QVariantMap &track) {
    return QJsonDocument::fromJson(track.value("geoJson").toString().toUtf8()).object()["features"].toArray();
}
static QString color(const QJsonValue &feature) { return feature.toObject()["properties"].toObject()["color"].toString(); }

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    const auto path = directory.filePath("track.gpx");
    GpxTrack reader;
    auto read = [&](const QByteArray &body) {
        QFile file(path); check(file.open(QIODevice::WriteOnly), "fixture opens");
        file.write(body); file.close();
        return reader.read(QUrl::fromLocalFile(path));
    };
    auto point = [](const QByteArray &lon, const QByteArray &body) {
        return "<trkpt lat='0' lon='" + lon + "'>" + body + "</trkpt>";
    };
    auto wrap = [](const QByteArray &points) { return "<gpx><trk><trkseg>" + points + "</trkseg></trk></gpx>"; };
    const auto red = read(wrap(point("0", "<speed>0</speed>") + point(".001", "<speed>0</speed>")));
    check(!red.contains("error") && red.value("recordedEdgeCount").toInt() == 1, "zero recorded speed is valid");
    check(color(features(red)[0]) == "#e53c3c", "stopped traffic is red");
    const auto blue = read(wrap(point("0", "<speed>40</speed>") + point(".001", "<speed>40</speed>")));
    check(color(features(blue)[0]) == "#245be6", "meters per second are converted and capped at blue");
    QString previousColor;
    for (int kmh : {50, 60, 80, 100, 120}) {
        const auto speed = "<speed>" + QByteArray::number(kmh / 3.6, 'f', 8) + "</speed>";
        const auto track = read(wrap(point("0", speed) + point(".001", speed)));
        const auto currentColor = color(features(track)[0]);
        check(currentColor != previousColor, "speeds from 50 through 120 remain distinguishable");
        check(track.value("speedStops").toList().last().toMap().value("speed").toInt() == 120, "legend matches 120 km/h scale");
        previousColor = currentColor;
    }
    const auto extension = read(wrap(point("0", "<extensions><gpxtpx:TrackPointExtension xmlns:gpxtpx='http://www.garmin.com/xmlschemas/TrackPointExtension/v2'><gpxtpx:speed>0</gpxtpx:speed></gpxtpx:TrackPointExtension></extensions>") + point(".001", "")));
    check(color(features(extension)[0]) == "#e53c3c", "Garmin extension and single measured endpoint");
    const auto averaged = read(wrap(point("0", "<speed>0</speed>") + point(".001", "<speed>22.2222222222</speed>")));
    check(color(features(averaged)[0]) == "#efa83d", "adjacent recorded speeds are averaged");
    const auto timed = read(wrap(point("0", "<time>2026-01-01T08:00:00.000+08:00</time><speed>nan</speed>")
        + point(".001", "<time>2026-01-01T00:00:10Z</time><speed>-1</speed>")
        + point(".002", "<time>2026-01-01T00:00:10Z</time>")
        + point(".003", "<time>2026-01-01T00:00:09Z</time>") + point(".004", "")));
    check(timed.value("derivedEdgeCount").toInt() == 1 && timed.value("unknownEdgeCount").toInt() == 3, "derive from timestamps; equal/reversed/missing time stays unknown");
    check(color(features(timed)[0]) == "#efa83d" && color(features(timed)[1]) == "#8a929e", "derived 40 km/h and unknown colors");
    const auto priority = read(wrap(point("0", "<speed>0</speed><time>2026-01-01T00:00:00Z</time>")
        + point(".001", "<speed>0</speed><time>2026-01-01T00:00:01Z</time>")));
    check(color(features(priority)[0]) == "#e53c3c", "recorded zero takes precedence over GPS displacement");
    const auto separated = read("<gpx><trk><trkseg>" + point("0", "") + point(".001", "")
        + "</trkseg><trkseg>" + point("10", "") + point("10.001", "") + "</trkseg></trk></gpx>");
    check(features(separated).size() == 2 && separated.value("distance").toDouble() < 225, "never join separate GPX segments");
    const auto route = read("<gpx><rte><rtept lat='0' lon='0'/><rtept lat='0' lon='.001'/></rte></gpx>");
    check(color(features(route)[0]) == "#8a929e", "route without speed remains visible in gray");
    check(read(wrap(point("999", ""))).contains("error"), "bad coordinate rejected");
    check(read(wrap(point("0", "") + point(".001", "")) + "<extra/>").contains("error"), "trailing invalid XML rejected");
    check(read("<gpx><trk><trkseg><trkpt lat='0' lon='0'/></trkseg><trkseg><trkpt lat='0' lon='1'/></trkseg></trk></gpx>").contains("error"), "isolated points do not form a drawable line");
    QByteArray many;
    for (int i = 0; i < 10000; ++i) many += point(QByteArray::number(i * .00001, 'f', 5), "<speed>1</speed>");
    const auto longTrack = read(wrap(many));
    check(features(longTrack).size() == 1 && longTrack.value("pointCount").toInt() == 10000, "long same-color tracks coalesce without losing points");
    for (int i = 1; i < argc; ++i) {
        const auto track = reader.read(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[i])));
        check(!track.contains("error"), "real GPX loads");
        printf("GPX: %s, points=%d recorded=%d derived=%d unknown=%d\n", qPrintable(QFileInfo(QString::fromLocal8Bit(argv[i])).fileName()),
            track.value("pointCount").toInt(), track.value("recordedEdgeCount").toInt(), track.value("derivedEdgeCount").toInt(), track.value("unknownEdgeCount").toInt());
    }
    puts("PASS: GPX speeds, units, timestamps, missing data, segment boundaries and coalescing");
}
