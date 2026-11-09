#include "graphstorage.h"
#include <QBitArray>
#include <QCryptographicHash>
#include <QDataStream>
#include <QJsonDocument>
#include <QSaveFile>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace {
constexpr int Header = 120;
quint32 u32(const uchar *p) { return qFromLittleEndian<quint32>(p); }
quint64 u64(const uchar *p) { return qFromLittleEndian<quint64>(p); }
double real(const uchar *p) {
    auto bits = u64(p);
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}
void integer(QByteArray &b, quint32 n) {
    char p[4];
    qToLittleEndian(n, p);
    b.append(p, 4);
}
void number(QByteArray &b, double n) {
    quint64 bits;
    std::memcpy(&bits, &n, 8);
    char p[8];
    qToLittleEndian(bits, p);
    b.append(p, 8);
}
int column(double lon) { return qBound(0, int(std::floor((lon + 180) * 100)), 35999); }
int row(double lat) { return qBound(0, int(std::floor((lat + 90) * 100)), 17999); }
quint32 cell(const QGeoCoordinate &c) { return row(c.latitude()) * 36000U + column(c.longitude()); }
} // namespace
bool GraphStorage::write(const QString &path, const QJsonObject &metadata, const QVector<RouteNode> &nodes,
                         const QStringList &names, const QStringList &roads, const QVector<bool> &local,
                         const QVector<RoutePrefix> &trie, const QVector<QVector<int>> &incoming,
                         QString *error) {
    QByteArray s[10];
    s[0] = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
    std::map<quint32, QVector<int>> grid;
    for (int i = 0; i < nodes.size(); ++i) {
        const auto &node = nodes[i];
        number(s[1], node.coordinate.longitude());
        number(s[1], node.coordinate.latitude());
        integer(s[1], s[2].size() / 28);
        integer(s[1], node.edges.size());
        integer(s[1], s[7].size() / 4);
        const auto in = incoming.value(i);
        integer(s[1], in.size());
        for (int v : in)
            integer(s[7], v);
        for (const auto &e : node.edges) {
            integer(s[2], e.destination);
            integer(s[2], e.way);
            number(s[2], e.distance);
            number(s[2], e.seconds);
            integer(s[2], e.token);
        }
        grid[cell(node.coordinate)].append(i);
    }
    QHash<QString, quint32> strings;
    auto intern = [&](const QString &text) {
        if (strings.contains(text))
            return strings.value(text);
        quint32 offset = s[4].size();
        auto bytes = text.toUtf8();
        integer(s[4], bytes.size());
        s[4].append(bytes);
        strings.insert(text, offset);
        return offset;
    };
    for (int i = 0; i < names.size(); ++i) {
        integer(s[3], intern(names[i]));
        integer(s[3], intern(roads[i]));
        integer(s[3], local[i] ? 1 : 0);
    }
    for (const auto &prefix : trie) {
        integer(s[5], prefix.fail);
        integer(s[5], prefix.forbidden ? 1 : 0);
        integer(s[5], s[6].size() / 8);
        integer(s[5], prefix.next.size());
        auto keys = prefix.next.keys();
        std::sort(keys.begin(), keys.end());
        for (int token : keys) {
            integer(s[6], token);
            integer(s[6], prefix.next.value(token));
        }
    }
    for (const auto &[key, ids] : grid) {
        integer(s[8], key);
        integer(s[8], s[9].size() / 4);
        integer(s[8], ids.size());
        for (int id : ids)
            integer(s[9], id);
    }
    QByteArray header("VRGRPH01", 8);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto &part : s) {
        char bytes[8];
        qToLittleEndian(quint64(part.size()), bytes);
        header.append(bytes, 8);
        hash.addData(part);
    }
    header.append(hash.result());
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(header) != header.size()) {
        *error = output.errorString();
        return false;
    }
    for (const auto &part : s)
        if (output.write(part) != part.size()) {
            *error = output.errorString();
            return false;
        }
    if (!output.commit()) {
        *error = output.errorString();
        return false;
    }
    return true;
}
std::unique_ptr<GraphStorage> GraphStorage::open(const QString &path, QString *error) {
    auto graph = std::unique_ptr<GraphStorage>(new GraphStorage);
    graph->file.setFileName(path);
    if (!graph->file.open(QIODevice::ReadOnly) || graph->file.size() < Header ||
        graph->file.size() > 512LL * 1024 * 1024) {
        *error = "无法读取路网索引或文件大小无效。";
        return {};
    }
    graph->mapping = graph->file.map(0, graph->file.size());
    if (!graph->mapping || std::memcmp(graph->mapping, "VRGRPH01", 8)) {
        *error = "路网索引版本不兼容。";
        return {};
    }
    quint64 offset = Header;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (int i = 0; i < 10; ++i) {
        auto size = u64(graph->mapping + 8 + i * 8);
        if (size > quint64(graph->file.size()) - offset) {
            *error = "路网索引被截断。";
            return {};
        }
        graph->sections[i] = graph->mapping + offset;
        graph->sizes[i] = size;
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(graph->sections[i]), size));
        offset += size;
    }
    if (offset != quint64(graph->file.size()) ||
        hash.result() != QByteArray(reinterpret_cast<char *>(graph->mapping + 88), 32)) {
        *error = "路网索引校验失败。";
        return {};
    }
    if (graph->sizes[0] > 65536) {
        *error = "路网元数据过大。";
        return {};
    }
    graph->metadata = QJsonDocument::fromJson(
                          QByteArray(reinterpret_cast<const char *>(graph->sections[0]), graph->sizes[0]))
                          .object();
    if (!graph->validate(error))
        return {};
    return graph;
}
GraphStorage::~GraphStorage() {
    if (mapping)
        file.unmap(mapping);
}
int GraphStorage::nodeCount() const { return sizes[1] / 32; }
QGeoCoordinate GraphStorage::coordinate(int node) const {
    const auto *p = sections[1] + node * 32;
    return {real(p + 8), real(p)};
}
int GraphStorage::edgeCount(int node) const { return u32(sections[1] + node * 32 + 20); }
RouteEdge GraphStorage::edge(int node, int index) const {
    const auto *p = sections[2] + (quint64(u32(sections[1] + node * 32 + 16)) + index) * 28;
    return {int(u32(p)), int(u32(p + 4)), real(p + 8), real(p + 16), int(u32(p + 24))};
}
bool GraphStorage::local(int way) const { return u32(sections[3] + way * 12 + 8); }
QString GraphStorage::string(quint32 offset) const {
    const auto *p = sections[4] + offset;
    return QString::fromUtf8(reinterpret_cast<const char *>(p + 4), u32(p));
}
QString GraphStorage::name(int way) const { return string(u32(sections[3] + way * 12)); }
QString GraphStorage::roadId(int way) const { return string(u32(sections[3] + way * 12 + 4)); }
int GraphStorage::incomingCount(int node) const { return u32(sections[1] + node * 32 + 28); }
int GraphStorage::incoming(int node, int index) const {
    return u32(sections[7] + (quint64(u32(sections[1] + node * 32 + 24)) + index) * 4);
}
bool GraphStorage::forbidden(int prefix) const { return u32(sections[5] + prefix * 16 + 4); }
int GraphStorage::transition(int prefix, int token) const {
    while (true) {
        const auto *p = sections[5] + prefix * 16;
        int first = u32(p + 8), last = first + u32(p + 12), end = last;
        while (first < last) {
            int mid = first + (last - first) / 2;
            if (u32(sections[6] + mid * 8) < quint32(token))
                first = mid + 1;
            else
                last = mid;
        }
        if (first < end && u32(sections[6] + first * 8) == quint32(token))
            return u32(sections[6] + first * 8 + 4);
        if (!prefix)
            return 0;
        prefix = u32(p);
    }
}
int GraphStorage::snap(const QGeoCoordinate &point, double &distance) const {
    int best = -1;
    distance = 1000;
    const double dlat = 1000.0 / 6371000 * 180 / M_PI * 1.01;
    const double lo = qMax(-90.0, point.latitude() - dlat), hi = qMin(90.0, point.latitude() + dlat);
    const double dlon =
        qMin(180.0, dlat / qMax(0.000001, std::cos(qMax(std::abs(lo), std::abs(hi)) * M_PI / 180)));
    double west = point.longitude() - dlon, east = point.longitude() + dlon;
    QVector<QPair<int, int>> columns;
    if (west < -180)
        columns = {{0, column(east)}, {column(west + 360), 35999}};
    else if (east > 180)
        columns = {{column(west), 35999}, {0, column(east - 360)}};
    else
        columns = {{column(west), column(east)}};
    for (int y = row(lo); y <= row(hi); ++y)
        for (const auto &range : columns) {
            quint32 min = y * 36000U + range.first, max = y * 36000U + range.second;
            int left = 0, right = sizes[8] / 12;
            while (left < right) {
                int mid = left + (right - left) / 2;
                if (u32(sections[8] + mid * 12) < min)
                    left = mid + 1;
                else
                    right = mid;
            }
            for (int i = left; i < int(sizes[8] / 12) && u32(sections[8] + i * 12) <= max; ++i) {
                const auto *p = sections[8] + i * 12;
                for (quint32 j = 0; j < u32(p + 8); ++j) {
                    int id = u32(sections[9] + (quint64(u32(p + 4)) + j) * 4);
                    double candidate = point.distanceTo(coordinate(id));
                    if (candidate < distance || (candidate == distance && id > best)) {
                        distance = candidate;
                        best = id;
                    }
                }
            }
        }
    return best;
}
bool GraphStorage::validate(QString *error) const {
    auto fail = [&] {
        *error = "路网索引结构或引用损坏。";
        return false;
    };
    const QString mode = metadata.value("mode").toString();
    if (mode != "car" && mode != "walk" && mode != "bicycle")
        return fail();
    const int stride[10] = {1, 32, 28, 12, 1, 16, 8, 4, 12, 4};
    for (int i = 1; i < 10; ++i)
        if (sizes[i] % stride[i])
            return fail();
    const auto n = nodeCount();
    const quint32 ways = sizes[3] / 12, prefixes = sizes[5] / 16;
    if (!metadata.value("error").toString().isEmpty())
        return true; // No traversal permitted for failed compiled graphs.
    if (!n || !prefixes || sizes[9] / 4 != quint64(n))
        return fail();
    auto span = [](quint32 start, quint32 count, quint64 total) { return quint64(start) + count <= total; };
    for (int i = 0; i < n; ++i) {
        const auto *p = sections[1] + i * 32;
        if (!coordinate(i).isValid() || !span(u32(p + 16), u32(p + 20), sizes[2] / 28) ||
            !span(u32(p + 24), u32(p + 28), sizes[7] / 4))
            return fail();
    }
    for (quint64 i = 0; i < sizes[2]; i += 28) {
        const auto *p = sections[2] + i;
        if (u32(p) >= quint32(n) || u32(p + 4) >= ways || u32(p + 24) > 0x7fffffff ||
            !std::isfinite(real(p + 8)) || real(p + 8) < 0 || !std::isfinite(real(p + 16)) ||
            real(p + 16) < 0)
            return fail();
    }
    for (quint32 i = 0; i < ways; ++i)
        for (int j = 0; j < 2; ++j) {
            quint32 offset = u32(sections[3] + i * 12 + j * 4);
            if (quint64(offset) + 4 > sizes[4] || quint64(offset) + 4 + u32(sections[4] + offset) > sizes[4])
                return fail();
        }
    if (u32(sections[5]) != 0 || forbidden(0))
        return fail();
    QVector<quint8> color(prefixes, 0);
    color[0] = 2;
    for (quint32 i = 0; i < prefixes; ++i) {
        const auto *p = sections[5] + i * 16;
        if (u32(p) >= prefixes || !span(u32(p + 8), u32(p + 12), sizes[6] / 8))
            return fail();
        quint32 previous = 0;
        for (quint32 j = 0; j < u32(p + 12); ++j) {
            const auto *t = sections[6] + (quint64(u32(p + 8)) + j) * 8;
            if (u32(t) > 0x7fffffff || u32(t + 4) >= prefixes || (j && u32(t) <= previous))
                return fail();
            previous = u32(t);
        }
    }
    for (quint32 i = 1; i < prefixes; ++i) {
        quint32 v = i;
        while (!color[v]) {
            color[v] = 1;
            v = u32(sections[5] + v * 16);
        }
        if (color[v] == 1)
            return fail();
        v = i;
        while (color[v] == 1) {
            color[v] = 2;
            v = u32(sections[5] + v * 16);
        }
    }
    for (quint64 i = 0; i < sizes[7]; i += 4)
        if (u32(sections[7] + i) >= quint32(n))
            return fail();
    QBitArray seen(n);
    quint32 previous = 0;
    for (quint64 i = 0; i < sizes[8] / 12; ++i) {
        const auto *p = sections[8] + i * 12;
        if ((i && u32(p) <= previous) || !span(u32(p + 4), u32(p + 8), sizes[9] / 4))
            return fail();
        previous = u32(p);
        for (quint32 j = 0; j < u32(p + 8); ++j) {
            quint32 id = u32(sections[9] + (quint64(u32(p + 4)) + j) * 4);
            if (id >= quint32(n) || seen[id] || cell(coordinate(id)) != previous)
                return fail();
            seen[id] = true;
        }
    }
    return seen.count(true) == n || fail();
}
