#include "geometryindex.h"
#include <QCborValue>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <cmath>
#include <stdexcept>

namespace bg = boost::geometry;
namespace {
using Point = bg::model::d2::point_xy<double>;
using Line = bg::model::linestring<Point>;
using Polygon = bg::model::polygon<Point>;
using Box = bg::model::box<Point>;
constexpr int Extent = 4096, Buffer = 64;
void check(bool ok, const QString &message) {
    if (!ok)
        throw std::runtime_error(message.toStdString());
}
void varint(QByteArray &out, quint64 n) {
    while (n >= 128) {
        out.append(char((n & 127) | 128));
        n >>= 7;
    }
    out.append(char(n));
}
void integer(QByteArray &out, int field, quint64 n) {
    varint(out, field << 3);
    varint(out, n);
}
void blob(QByteArray &out, int field, const QByteArray &bytes) {
    varint(out, (field << 3) | 2);
    varint(out, bytes.size());
    out.append(bytes);
}
quint64 zigzag(qint64 n) { return (quint64(n) << 1) ^ quint64(n >> 63); }
Point mercator(const QJsonArray &p) {
    check(p.size() == 2 && p[0].isDouble() && p[1].isDouble(), "几何坐标损坏。");
    double lon = p[0].toDouble(), lat = p[1].toDouble();
    check(std::isfinite(lon) && std::isfinite(lat) && std::abs(lon) <= 180 && std::abs(lat) <= 85.051129,
          "几何坐标超出范围。");
    double radians = qBound(-85.05112878, lat, 85.05112878) * M_PI / 180;
    return {(lon + 180) / 360, (1 - std::asinh(std::tan(radians)) / M_PI) / 2};
}
QJsonArray pair(const Point &p) { return {p.x(), p.y()}; }
Line worldLine(const QJsonArray &coordinates) {
    Line line;
    for (const auto &v : coordinates) {
        auto p = mercator(v.toArray());
        if (!line.empty()) {
            while (p.x() - line.back().x() > .5)
                p.x(p.x() - 1);
            while (p.x() - line.back().x() < -.5)
                p.x(p.x() + 1);
        }
        line.push_back(p);
    }
    return line;
}
template <typename Ring> QJsonArray array(const Ring &ring) {
    QJsonArray a;
    for (const auto &p : ring)
        a.append(pair(p));
    return a;
}
QJsonArray polygonArray(const Polygon &poly) {
    QJsonArray a;
    a.append(array(poly.outer()));
    for (const auto &ring : poly.inners())
        a.append(array(ring));
    return a;
}
Point project(const QJsonArray &pair, double scale, int x, int y, int shift) {
    check(pair.size() == 2 && pair[0].isDouble() && pair[1].isDouble(), "几何索引坐标损坏。");
    double px = (pair[0].toDouble() + shift) * scale - x * double(Extent),
           py = pair[1].toDouble() * scale - y * double(Extent);
    check(std::isfinite(px) && std::isfinite(py), "几何索引坐标无效。");
    return {px, py};
}
// MVT rings use clockwise screen coordinates (positive signed area); holes are reversed.
template <typename Range>
void appendPath(QByteArray &commands, const Range &points, bool ring, bool hole, int &cursorX, int &cursorY) {
    QVector<QPair<int, int>> path;
    for (const auto &p : points) {
        auto v = qMakePair(int(std::llround(p.x())), int(std::llround(p.y())));
        if (path.isEmpty() || path.last() != v)
            path.append(v);
    }
    if (ring && path.size() > 1 && path.first() == path.last())
        path.removeLast();
    if (path.size() < (ring ? 3 : 2))
        return;
    if (ring) {
        qint64 area = 0;
        for (int i = 0; i < path.size(); ++i) {
            auto a = path[i], b = path[(i + 1) % path.size()];
            area += qint64(a.first) * b.second - qint64(b.first) * a.second;
        }
        if (!area)
            return;
        if ((area > 0) == hole)
            std::reverse(path.begin(), path.end());
    }
    varint(commands, 9);
    varint(commands, zigzag(path[0].first - cursorX));
    varint(commands, zigzag(path[0].second - cursorY));
    cursorX = path[0].first;
    cursorY = path[0].second;
    varint(commands, (quint64(path.size() - 1) << 3) | 2);
    for (int i = 1; i < path.size(); ++i) {
        varint(commands, zigzag(path[i].first - cursorX));
        varint(commands, zigzag(path[i].second - cursorY));
        cursorX = path[i].first;
        cursorY = path[i].second;
    }
    if (ring)
        varint(commands, 15);
}
QByteArray geometry(int kind, const QJsonArray &coordinates, int z, int x, int y, int shift) {
    double scale = std::ldexp(double(Extent), z);
    const Box bounds(Point(-Buffer, -Buffer), Point(Extent + Buffer, Extent + Buffer));
    QByteArray commands;
    int cx = 0, cy = 0;
    if (kind == 1) {
        auto p = project(coordinates, scale, x, y, shift);
        if (!bg::covered_by(p, bounds))
            return {};
        varint(commands, 9);
        varint(commands, zigzag(std::llround(p.x())));
        varint(commands, zigzag(std::llround(p.y())));
    } else if (kind == 2) {
        Line line;
        for (const auto &v : coordinates)
            line.push_back(project(v.toArray(), scale, x, y, shift));
        std::vector<Line> clipped;
        bg::intersection(line, bounds, clipped);
        for (const auto &part : clipped)
            appendPath(commands, part, false, false, cx, cy);
    } else {
        Polygon poly;
        for (int i = 0; i < coordinates.size(); ++i) {
            if (i)
                poly.inners().resize(i);
            auto &ring = i ? poly.inners()[i - 1] : poly.outer();
            for (const auto &v : coordinates[i].toArray())
                ring.push_back(project(v.toArray(), scale, x, y, shift));
        }
        bg::correct(poly);
        std::vector<Polygon> clipped;
        bg::intersection(poly, bounds, clipped);
        for (const auto &part : clipped) {
            appendPath(commands, part.outer(), true, false, cx, cy);
            for (const auto &hole : part.inners())
                appendPath(commands, hole, true, true, cx, cy);
        }
    }
    return commands;
}
} // namespace
struct GeometryIndex::Impl {
    QString connection;
    QSqlDatabase db;
    std::unique_ptr<QSqlQuery> feature, shape, bounds;
    qint64 next = 0;
    ~Impl() {
        feature.reset();
        shape.reset();
        bounds.reset();
        db = {};
        QSqlDatabase::removeDatabase(connection);
    }
};
GeometryIndex::GeometryIndex(const QString &path, bool create) : impl(new Impl) {
    impl->connection = QUuid::createUuid().toString();
    impl->db = QSqlDatabase::addDatabase("QSQLITE", impl->connection);
    impl->db.setDatabaseName(path);
    if (!create)
        impl->db.setConnectOptions("QSQLITE_OPEN_READONLY");
    check(impl->db.open(), "无法打开几何索引。");
    QSqlQuery q(impl->db);
    check(q.exec("PRAGMA cache_size=-4096"), q.lastError().text());
    if (!create) {
        check(q.exec("PRAGMA user_version") && q.next() && q.value(0).toInt() == 1, "几何索引版本不兼容。");
        return;
    }
    check(q.exec("PRAGMA user_version=1"), q.lastError().text());
    check(
        q.exec("CREATE TABLE features(id INTEGER PRIMARY KEY,kind INTEGER,minzoom INTEGER,properties BLOB)"),
        q.lastError().text());
    check(q.exec("CREATE TABLE shapes(feature INTEGER,level INTEGER,geometry BLOB,PRIMARY "
                 "KEY(feature,level)) WITHOUT ROWID"),
          q.lastError().text());
    check(q.exec("CREATE VIRTUAL TABLE bounds USING rtree(id,west,east,north,south)"), q.lastError().text());
    check(impl->db.transaction(), "无法启动几何索引事务。");
    impl->feature = std::make_unique<QSqlQuery>(impl->db);
    impl->shape = std::make_unique<QSqlQuery>(impl->db);
    impl->bounds = std::make_unique<QSqlQuery>(impl->db);
    check(impl->feature->prepare("INSERT INTO features VALUES(?,?,?,?)"), "几何索引语句无效。");
    check(impl->shape->prepare("INSERT INTO shapes VALUES(?,?,?)"), "几何索引语句无效。");
    check(impl->bounds->prepare("INSERT INTO bounds VALUES(?,?,?,?,?)"), "几何范围语句无效。");
}
GeometryIndex::~GeometryIndex() = default;
void GeometryIndex::commit() { check(impl->db.commit(), "无法完成几何索引事务。"); }
void GeometryIndex::add(const QJsonObject &feature) {
    const auto shape = feature.value("geometry").toObject();
    const auto coordinates = shape.value("coordinates").toArray();
    auto type = shape.value("type").toString();
    int kind = type == "Point" ? 1 : type == "LineString" ? 2 : type == "Polygon" ? 3 : 0;
    check(kind != 0, "不支持的几何类型。");
    Point point;
    Line line;
    Polygon polygon;
    Box box;
    if (kind == 1) {
        point = mercator(coordinates);
        box = Box(point, point);
    } else if (kind == 2) {
        line = worldLine(coordinates);
        check(line.size() >= 2, "线段顶点不足。");
        bg::envelope(line, box);
    } else {
        check(!coordinates.isEmpty(), "多边形没有外环。");
        for (int i = 0; i < coordinates.size(); ++i) {
            auto ring = worldLine(coordinates[i].toArray());
            check(ring.size() >= 3, "多边形顶点不足。");
            if (i) {
                double shift = std::round(polygon.outer().front().x() - ring.front().x());
                for (auto &p : ring)
                    p.x(p.x() + shift);
                polygon.inners().resize(i);
                bg::assign_points(polygon.inners()[i - 1], ring);
            } else
                bg::assign_points(polygon.outer(), ring);
        }
        bg::correct(polygon);
        bg::envelope(polygon, box);
    }
    QJsonObject properties;
    const auto source = feature.value("properties").toObject();
    for (const auto &key : {"name", "class", "category"})
        if (source.value(key).isString())
            properties.insert(key, source.value(key));
    QString category = properties.value("class").toString();
    int minzoom = kind == 1               ? 14
                  : category == "major"   ? 5
                  : category == "rail"    ? 10
                  : category == "contour" ? 11
                  : category == "trail"   ? 12
                                          : 9;
    if (kind == 3) {
        double area = std::abs(bg::area(polygon));
        minzoom = area > 0 ? qBound(0, int(std::ceil(std::log2(2 / (512 * std::sqrt(area))))), 14) : 14;
    }
    qint64 id = ++impl->next;
    auto &insert = *impl->feature;
    insert.bindValue(0, id);
    insert.bindValue(1, kind);
    insert.bindValue(2, minzoom);
    insert.bindValue(3, QCborValue::fromJsonValue(properties).toCbor());
    check(insert.exec(), insert.lastError().text());
    for (int shift = -1; shift <= 1; ++shift) {
        double west = box.min_corner().x() + shift, east = box.max_corner().x() + shift;
        if (east < 0 || west > 1)
            continue;
        auto &q = *impl->bounds;
        q.bindValue(0, id * 3 + shift + 1);
        q.bindValue(1, west);
        q.bindValue(2, east);
        q.bindValue(3, box.min_corner().y());
        q.bindValue(4, box.max_corner().y());
        check(q.exec(), q.lastError().text());
    }
    for (int level : {8, 11, 14}) {
        if (minzoom > level)
            continue;
        QJsonArray result;
        double tolerance = 2 / std::ldexp(double(Extent), level);
        if (kind == 1)
            result = pair(point);
        else if (kind == 2) {
            Line simple;
            bg::simplify(line, simple, tolerance);
            result = array(simple);
        } else {
            Polygon simple;
            bg::simplify(polygon, simple, tolerance);
            bg::correct(simple);
            result = polygonArray(bg::is_valid(simple) ? simple : polygon);
        }
        auto &q = *impl->shape;
        q.bindValue(0, id);
        q.bindValue(1, level);
        q.bindValue(2, QCborValue::fromJsonValue(result).toCbor());
        check(q.exec(), q.lastError().text());
    }
}
QByteArray GeometryIndex::tile(int zoom, int x, int y, int *featuresRead) const {
    check(zoom >= 0 && zoom <= 14 && x >= 0 && y >= 0 && x < (1 << zoom) && y < (1 << zoom),
          "瓦片坐标无效。");
    if (featuresRead)
        *featuresRead = 0;
    double n = std::ldexp(1.0, zoom), buffer = double(Buffer) / Extent;
    QSqlQuery q(impl->db);
    q.setForwardOnly(true);
    check(q.prepare("SELECT f.id,f.kind,f.properties,s.geometry,r.id%3-1 FROM bounds r "
                    "JOIN features f ON f.id=CAST(r.id/3 AS INTEGER) JOIN shapes s ON s.feature=f.id "
                    "WHERE r.east>=:west AND r.west<=:east AND r.south>=:north AND r.north<=:south "
                    "AND f.minzoom<=:zoom AND s.level=:level"),
          q.lastError().text());
    q.bindValue(":west", (x - buffer) / n);
    q.bindValue(":east", (x + 1 + buffer) / n);
    q.bindValue(":north", (y - buffer) / n);
    q.bindValue(":south", (y + 1 + buffer) / n);
    q.bindValue(":zoom", zoom);
    q.bindValue(":level", zoom <= 8 ? 8 : zoom <= 11 ? 11 : 14);
    check(q.exec(), q.lastError().text());
    QByteArray layer;
    blob(layer, 1, "garmin");
    QStringList keys, values;
    QHash<QString, int> keyIds, valueIds;
    while (q.next()) {
        if (featuresRead)
            ++*featuresRead;
        const auto bytes = q.value(3).toByteArray();
        check(bytes.size() <= 16 * 1024 * 1024, "单个几何对象过大。");
        QCborParserError error;
        auto shape = QCborValue::fromCbor(bytes, &error);
        check(error.error == QCborError::NoError && shape.isArray(), "几何索引对象损坏。");
        QByteArray commands =
            geometry(q.value(1).toInt(), shape.toJsonValue().toArray(), zoom, x, y, q.value(4).toInt());
        if (commands.isEmpty())
            continue;
        const auto properties = QCborValue::fromCbor(q.value(2).toByteArray()).toJsonValue().toObject();
        QByteArray tags;
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            if ((it.key() == "name" && zoom < 12) || (it.key() == "category" && zoom < 14))
                continue;
            if (!keyIds.contains(it.key())) {
                keyIds.insert(it.key(), keys.size());
                keys.append(it.key());
            }
            QString value = it.value().toString();
            if (!valueIds.contains(value)) {
                valueIds.insert(value, values.size());
                values.append(value);
            }
            varint(tags, keyIds.value(it.key()));
            varint(tags, valueIds.value(value));
        }
        QByteArray feature;
        integer(feature, 1, q.value(0).toLongLong());
        blob(feature, 2, tags);
        integer(feature, 3, q.value(1).toInt());
        blob(feature, 4, commands);
        blob(layer, 2, feature);
        check(layer.size() <= 8 * 1024 * 1024, "瓦片数据超过容量限制。");
    }
    for (const auto &key : keys)
        blob(layer, 3, key.toUtf8());
    for (const auto &value : values) {
        QByteArray v;
        blob(v, 1, value.toUtf8());
        blob(layer, 4, v);
    }
    integer(layer, 5, Extent);
    integer(layer, 15, 2);
    check(layer.size() <= 8 * 1024 * 1024, "瓦片数据超过容量限制。");
    QByteArray tile;
    blob(tile, 3, layer);
    return tile;
}
