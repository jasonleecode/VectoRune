#include "gpxtrack.h"

#include <QFile>
#include <QFileInfo>
#include <QGeoCoordinate>
#include <QGeoRectangle>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QXmlStreamReader>
#include <array>
#include <cmath>

namespace {
constexpr int SpeedScaleMaximum = 120;
constexpr int SpeedScaleInterval = SpeedScaleMaximum / 3;
struct TrackPoint {
    QGeoCoordinate coordinate;
    QDateTime time;
    double speed = -1; // meters/second; zero is a valid recorded speed
};

// GPX 1.0 speed and the supported device extensions use meters/second.
// https://www.topografix.com/gpx_manual.asp
bool speedNamespace(QStringView uri) {
    return uri.isEmpty() || uri == u"http://www.topografix.com/GPX/1/0"
        || uri == u"http://www.topografix.com/GPX/1/1"
        || uri == u"http://www.garmin.com/xmlschemas/TrackPointExtension/v1"
        || uri == u"http://www.garmin.com/xmlschemas/TrackPointExtension/v2"
        || uri == u"http://www.cluetrust.com/XML/GPXDATA/1/0";
}

TrackPoint readPoint(QXmlStreamReader &xml) {
    TrackPoint point;
    bool latOk, lonOk;
    const auto lat = xml.attributes().value("lat").toDouble(&latOk);
    const auto lon = xml.attributes().value("lon").toDouble(&lonOk);
    point.coordinate = QGeoCoordinate(lat, lon);
    if (!latOk || !lonOk || !point.coordinate.isValid()) {
        xml.raiseError(QObject::tr("轨迹中包含无效经纬度。"));
        return point;
    }
    int depth = 1;
    while (!xml.atEnd() && depth > 0) {
        xml.readNext();
        if (xml.isStartElement()) {
            ++depth;
            if (depth == 2 && xml.name() == u"time") {
                point.time = QDateTime::fromString(xml.readElementText(), Qt::ISODateWithMs);
                --depth;
            } else if (xml.name() == u"speed" && speedNamespace(xml.namespaceUri())) {
                bool ok;
                const auto speed = xml.readElementText().trimmed().toDouble(&ok);
                if (ok && std::isfinite(speed) && speed >= 0) point.speed = speed;
                --depth;
            }
        } else if (xml.isEndElement()) --depth;
    }
    return point;
}

QString speedColor(int band) {
    if (band < 0) return "#8a929e";
    static constexpr std::array<std::array<int, 3>, 4> colors{{
        {{229, 60, 60}}, {{239, 168, 61}}, {{58, 182, 215}}, {{36, 91, 230}}
    }};
    const int index = qMin(band / SpeedScaleInterval, 2);
    const double fraction = double(band - index * SpeedScaleInterval) / SpeedScaleInterval;
    QString result = "#";
    for (int component = 0; component < 3; ++component) {
        const int value = qRound(colors[index][component] * (1 - fraction)
                                 + colors[index + 1][component] * fraction);
        result += QString::number(value, 16).rightJustified(2, '0');
    }
    return result;
}

QJsonArray position(const QGeoCoordinate &coordinate) {
    return {coordinate.longitude(), coordinate.latitude()};
}
}

QVariantMap GpxTrack::read(const QUrl &url) const
{
    auto fail = [](const QString &message) { return QVariantMap{{"error", message}}; };
    QFile file(url.toLocalFile());
    if (!url.isLocalFile() || !file.open(QIODevice::ReadOnly))
        return fail(tr("无法读取所选 GPX 文件。"));
    QXmlStreamReader xml(&file);
    if (!xml.readNextStartElement() || xml.name() != u"gpx")
        return fail(tr("所选文件不是 GPX 轨迹。"));

    QVariantList segments;
    QList<QGeoCoordinate> coordinates;
    QJsonArray features;
    double distance = 0;
    int recordedEdges = 0, derivedEdges = 0, unknownEdges = 0;
    auto readSegment = [&](QStringView pointName) {
        QVariantList segment;
        TrackPoint previous;
        QJsonArray coloredPath;
        int previousBand = -2;
        auto flushColor = [&] {
            if (coloredPath.size() >= 2) {
                features.append(QJsonObject{{"type", "Feature"},
                    {"properties", QJsonObject{{"color", speedColor(previousBand)}}},
                    {"geometry", QJsonObject{{"type", "LineString"}, {"coordinates", coloredPath}}}});
            }
            coloredPath = {};
        };
        while (xml.readNextStartElement()) {
            if (xml.name() != pointName) { xml.skipCurrentElement(); continue; }
            const auto point = readPoint(xml);
            if (xml.hasError()) return;
            if (!segment.isEmpty()) {
                const double length = previous.coordinate.distanceTo(point.coordinate);
                distance += length;
                double speed = -1;
                if (previous.speed >= 0 || point.speed >= 0) {
                    speed = previous.speed >= 0 && point.speed >= 0
                          ? previous.speed / 2 + point.speed / 2 : qMax(previous.speed, point.speed);
                    ++recordedEdges;
                } else if (previous.time.isValid() && point.time.isValid()
                           && previous.time.msecsTo(point.time) > 0) {
                    speed = length * 1000 / previous.time.msecsTo(point.time);
                    ++derivedEdges;
                } else ++unknownEdges;
                // Quantize to 1 km/h and join adjacent equal colors. A single
                // GeoJSON source avoids thousands of QML MapPolyline objects.
                const int band = speed < 0 ? -1 : qRound(qMin(speed * 3.6, double(SpeedScaleMaximum)));
                if (band != previousBand) {
                    flushColor();
                    coloredPath.append(position(previous.coordinate));
                    previousBand = band;
                }
                coloredPath.append(position(point.coordinate));
            }
            segment.append(QVariant::fromValue(point.coordinate));
            coordinates.append(point.coordinate);
            previous = point;
        }
        flushColor();
        if (!segment.isEmpty()) segments.append(QVariant(segment));
    };
    while (xml.readNextStartElement()) {
        if (xml.name() == u"trk") {
            while (xml.readNextStartElement()) {
                if (xml.name() == u"trkseg") readSegment(u"trkpt");
                else xml.skipCurrentElement();
            }
        } else if (xml.name() == u"rte") readSegment(u"rtept");
        else xml.skipCurrentElement();
    }
    // Consume the document end so malformed trailing XML is also rejected.
    while (!xml.atEnd()) xml.readNext();
    if (xml.hasError())
        return fail(tr("GPX 解析失败（第 %1 行）：%2").arg(xml.lineNumber()).arg(xml.errorString()));
    if (features.isEmpty())
        return fail(tr("文件中没有可显示的轨迹，请选择含 trkpt 或 rtept 的 GPX 文件。"));
    // Small padding makes the complete track visible, including its endpoints.
    QGeoRectangle bounds(coordinates);
    bounds.setWidth(qMax(bounds.width() * 1.15, 0.001));
    bounds.setHeight(qMax(bounds.height() * 1.15, 0.001));
    QVariantList speedStops;
    for (int speed = 0; speed <= SpeedScaleMaximum; speed += SpeedScaleInterval)
        speedStops.append(QVariantMap{{"speed", speed}, {"color", speedColor(speed)}});
    const auto geoJson = QString::fromUtf8(QJsonDocument(QJsonObject{
        {"type", "FeatureCollection"}, {"features", features}}).toJson(QJsonDocument::Compact));
    return {{"name", QFileInfo(file).completeBaseName()}, {"segments", segments},
            {"geoJson", geoJson}, {"speedStops", speedStops},
            {"recordedEdgeCount", recordedEdges}, {"derivedEdgeCount", derivedEdges},
            {"unknownEdgeCount", unknownEdges}, {"unknownSpeedColor", speedColor(-1)},
            {"pointCount", coordinates.size()}, {"distance", distance},
            {"bounds", QVariant::fromValue(bounds)},
            {"start", QVariant::fromValue(coordinates.first())},
            {"end", QVariant::fromValue(coordinates.last())},
            {"folder", QUrl::fromLocalFile(QFileInfo(file).absolutePath())}};
}
