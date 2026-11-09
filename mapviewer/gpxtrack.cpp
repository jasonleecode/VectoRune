#include "gpxtrack.h"

#include <QFile>
#include <QFileInfo>
#include <QGeoCoordinate>
#include <QGeoRectangle>
#include <QXmlStreamReader>

QVariantMap GpxTrack::read(const QUrl &url) const
{
    auto fail = [](const QString &message) { return QVariantMap{{"error", message}}; };
    QFile file(url.toLocalFile());
    if (!url.isLocalFile() || !file.open(QIODevice::ReadOnly))
        return fail(tr("无法读取所选 GPX 文件。"));
    QXmlStreamReader xml(&file);
    if (!xml.readNextStartElement() || xml.name() != u"gpx")
        return fail(tr("所选文件不是 GPX 轨迹。"));

    QVariantList segments, segment;
    QList<QGeoCoordinate> coordinates;
    double distance = 0;
    QString segmentType;
    auto flush = [&] {
        if (!segment.isEmpty()) segments.append(QVariant(segment));
        segment.clear();
    };
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto name = xml.name();
            if (name == u"trkseg" || name == u"rte") {
                flush();
                segmentType = name.toString();
            } else if ((name == u"trkpt" && segmentType == "trkseg")
                       || (name == u"rtept" && segmentType == "rte")) {
                bool latOk, lonOk;
                const double lat = xml.attributes().value("lat").toDouble(&latOk);
                const double lon = xml.attributes().value("lon").toDouble(&lonOk);
                const QGeoCoordinate point(lat, lon);
                if (!latOk || !lonOk || !point.isValid())
                    return fail(tr("轨迹中包含无效经纬度（第 %1 行）。").arg(xml.lineNumber()));
                if (!segment.isEmpty())
                    distance += segment.last().value<QGeoCoordinate>().distanceTo(point);
                segment.append(QVariant::fromValue(point));
                coordinates.append(point);
            }
        } else if (xml.isEndElement() && xml.name() == segmentType) {
            flush();
            segmentType.clear();
        }
    }
    if (xml.hasError())
        return fail(tr("GPX 解析失败（第 %1 行）：%2").arg(xml.lineNumber()).arg(xml.errorString()));
    flush();
    if (coordinates.size() < 2)
        return fail(tr("文件中没有可显示的轨迹，请选择含 trkpt 或 rtept 的 GPX 文件。"));
    // Small padding makes the complete track visible, including its endpoints.
    QGeoRectangle bounds(coordinates);
    bounds.setWidth(qMax(bounds.width() * 1.15, 0.001));
    bounds.setHeight(qMax(bounds.height() * 1.15, 0.001));
    return {{"name", QFileInfo(file).completeBaseName()}, {"segments", segments},
            {"pointCount", coordinates.size()}, {"distance", distance},
            {"bounds", QVariant::fromValue(bounds)},
            {"start", QVariant::fromValue(coordinates.first())},
            {"end", QVariant::fromValue(coordinates.last())},
            {"folder", QUrl::fromLocalFile(QFileInfo(file).absolutePath())}};
}
