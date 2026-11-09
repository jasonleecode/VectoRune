// Offline OpenMapTiles name lookup. Runs in a worker process so tile scanning
// cannot block the map UI. MVT is a length-delimited protobuf wire format.
#include <QCoreApplication>
#include "placeindex.h"
#include "importcache.h"
#include "runtimepaths.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTextStream>
#include <QVariant>
#include <cmath>
#include <stdexcept>
#include <zlib.h>

struct Field {
    int number;
    quint64 value;
    QByteArray bytes;
};
static quint64 varint(const QByteArray &b, int &p) {
    quint64 v = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (p >= b.size())
            throw std::runtime_error("truncated protobuf");
        auto c = quint8(b[p++]);
        v |= quint64(c & 127) << shift;
        if (!(c & 128))
            return v;
    }
    throw std::runtime_error("invalid protobuf varint");
}
static QList<Field> fields(const QByteArray &b) {
    QList<Field> result;
    for (int p = 0; p < b.size();) {
        auto tag = varint(b, p);
        Field f{int(tag >> 3), 0, {}};
        switch (tag & 7) {
        case 0:
            f.value = varint(b, p);
            break;
        case 2: {
            auto n = varint(b, p);
            if (n > quint64(b.size() - p))
                throw std::runtime_error("invalid field size");
            f.bytes = b.mid(p, int(n));
            p += int(n);
            break;
        }
        case 1:
            p += 8;
            break;
        case 5:
            p += 4;
            break;
        default:
            throw std::runtime_error("unsupported protobuf wire type");
        }
        if (p > b.size())
            throw std::runtime_error("truncated field");
        result.append(f);
    }
    return result;
}
static QString folded(QString s) {
    s = s.normalized(QString::NormalizationForm_D).toCaseFolded();
    QString out;
    for (auto c : s)
        if (c.category() != QChar::Mark_NonSpacing)
            out += c;
    return out.trimmed();
}
static QByteArray unpack(QByteArray b) {
    if (b.size() < 2 || (quint8(b[0]) != 0x1f && quint8(b[0]) != 0x78))
        return b;
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef *>(b.data());
    stream.avail_in = b.size();
    if (inflateInit2(&stream, 15 + 32) != Z_OK)
        throw std::runtime_error("invalid compressed tile");
    QByteArray out;
    int status;
    do {
        char chunk[65536];
        stream.next_out = reinterpret_cast<Bytef *>(chunk);
        stream.avail_out = sizeof chunk;
        status = inflate(&stream, Z_NO_FLUSH);
        out.append(chunk, sizeof chunk - stream.avail_out);
        if (out.size() > 32 * 1024 * 1024) {
            inflateEnd(&stream);
            throw std::runtime_error("tile too large");
        }
    } while (status == Z_OK);
    inflateEnd(&stream);
    if (status != Z_STREAM_END)
        throw std::runtime_error("damaged compressed tile");
    return out;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("VectoRune");
    app.setApplicationName("Mapviewer");
    auto finish = [&](QJsonObject r) {
        QTextStream(stdout) << QJsonDocument(r).toJson(QJsonDocument::Compact);
    };
    if (argc != 3)
        return 1;
    try {
        auto db = QSqlDatabase::addDatabase("QSQLITE");
        db.setDatabaseName(QString::fromLocal8Bit(argv[1]));
        db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (!db.open())
            throw std::runtime_error("无法读取 MBTiles 地图。");
        QSqlQuery q(db);
        q.exec("SELECT value FROM metadata WHERE name='format'");
        if (!q.next() || q.value(0).toString() != "pbf")
            throw std::runtime_error("栅格地图没有可搜索的地名数据。");
        const auto term = folded(QString::fromLocal8Bit(argv[2]));
        QString cacheError;
        auto cache = ImportCache::acquire(QString::fromLocal8Bit(argv[1]), "mbtiles-places-v1", &cacheError,
            RuntimePaths::cacheDirectory() + "/places", 128LL * 1024 * 1024);
        if (!cache) throw std::runtime_error(cacheError.toStdString());
        const auto indexPath = cache->path() + "/places.sqlite";
        if (cache->ready()) {
            finish({{"items", PlaceIndex(indexPath, false).search(term)}});
            return 0;
        }
        {
        PlaceIndex index(indexPath, true);
        if (!q.exec("SELECT zoom_level,tile_column,tile_row,tile_data FROM tiles WHERE zoom_level=(SELECT "
                    "MAX(zoom_level) FROM tiles)"))
            throw std::runtime_error("无法读取地图瓦片。");
        while (q.next()) {
            int z = q.value(0).toInt();
            if (z < 0 || z > 24)
                continue;
            double n = std::ldexp(1.0, z), tx = q.value(1).toDouble(), ty = n - 1 - q.value(2).toDouble();
            for (const auto &layerField : fields(unpack(q.value(3).toByteArray()))) {
                if (layerField.number != 3)
                    continue;
                QStringList keys, values;
                QList<QByteArray> features;
                int extent = 4096, version = 1;
                for (const auto &f : fields(layerField.bytes)) {
                    if (f.number == 2)
                        features.append(f.bytes);
                    if (f.number == 3)
                        keys.append(QString::fromUtf8(f.bytes));
                    if (f.number == 4) {
                        QString value;
                        for (const auto &v : fields(f.bytes))
                            if (v.number == 1)
                                value = QString::fromUtf8(v.bytes);
                        values.append(value);
                    }
                    if (f.number == 5)
                        extent = int(f.value);
                    if (f.number == 15)
                        version = int(f.value);
                }
                if (version != 1 && version != 2)
                    throw std::runtime_error("不支持此矢量瓦片版本。");
                if (extent <= 0)
                    continue;
                for (const auto &feature : features) {
                    QByteArray tags, geometry;
                    for (const auto &f : fields(feature)) {
                        if (f.number == 2)
                            tags = f.bytes;
                        if (f.number == 4)
                            geometry = f.bytes;
                    }
                    QString name;
                    for (int p = 0; p < tags.size();) {
                        auto k = varint(tags, p), v = varint(tags, p);
                        if (k >= quint64(keys.size()) || v >= quint64(values.size()))
                            continue;
                        if (keys[k] == "name" && name.isEmpty())
                            name = values[v];
                        if ((keys[k] == "name:latin" || keys[k] == "name:nonlatin" || keys[k] == "name:en") &&
                            name.isEmpty())
                            name = values[v];
                        if (keys[k] == "name:zh" || keys[k] == "name_zh")
                            name = values[v];
                    }
                    if (name.isEmpty() || geometry.isEmpty())
                        continue;
                    int p = 0;
                    auto command = varint(geometry, p);
                    if ((command & 7) != 1 || !(command >> 3))
                        continue;
                    auto zig = [](quint64 v) { return qint64(v >> 1) ^ -qint64(v & 1); };
                    double x = zig(varint(geometry, p)), y = zig(varint(geometry, p));
                    double lon = (tx + x / extent) / n * 360 - 180;
                    double lat = std::atan(std::sinh(M_PI * (1 - 2 * (ty + y / extent) / n))) * 180 / M_PI;
                    QString identity = name + QString::number(lon, 'f', 3) + QString::number(lat, 'f', 3);
                    index.add(QJsonObject{{"name", name}, {"longitude", lon}, {"latitude", lat},
                                          {"subtitle", "本地矢量地图"}}, identity);
                }
            }
        }
        index.commit();
        }
        if (!cache->commit({"places.sqlite"}, &cacheError)) throw std::runtime_error(cacheError.toStdString());
        finish({{"items", PlaceIndex(indexPath, false).search(term)}});
    } catch (const std::exception &e) {
        finish({{"error", QString::fromUtf8(e.what())}});
        return 2;
    }
}
