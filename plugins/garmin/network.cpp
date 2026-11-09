// Garmin NOD Table A/C decoding. Format reference: mkgmap net/{RouteCenter,
// TableA,TableC,RouteRestriction,RouteNode}.java (GPL-2.0).
#include "network.h"
#include <QHash>
#include <QJsonArray>
#include <stdexcept>
extern "C" {
// clang-format off: legacy headers depend on this declaration order.
#include "libgarmin.h"
#include "libgarmin_priv.h"
#include "garmin_rgn.h"
#include "garmin_fat.h"
#include "garmin_net.h"
#define class garmin_class
#include "garmin_nod.h"
#undef class
// clang-format on
}
#undef log
namespace {
QHash<void *, QHash<int, QJsonObject>> rules;
struct Bytes {
    QByteArray data;
    int u(int p, int n = 1) const {
        if (p < 0 || n < 1 || p > data.size() - n)
            throw std::runtime_error("truncated NOD");
        unsigned v = 0;
        for (int i = 0; i < n; ++i)
            v |= unsigned(quint8(data[p + i])) << (8 * i);
        return int(v);
    }
    int s(int p, int n) const {
        int v = u(p, n), sign = 1 << (n * 8 - 1);
        return (v ^ sign) - sign;
    }
    int table(int node, int align) const { return ((node >> align) + 1 + u(node)) << align; }
    QJsonArray coordinate(int node, int align) const {
        int t = table(node, align), flags = u(node + 1), x, y;
        if (flags & 0x20) {
            x = s(node + 2, 2);
            y = s(node + 4, 2);
        } else {
            int v = u(node + 2, 3);
            x = (v & 0xfff);
            if (x & 0x800)
                x -= 0x1000;
            y = (v >> 12);
            if (y & 0x800)
                y -= 0x1000;
        }
        return {double(s(t + 1, 3) + x) * 360 / 16777216, double(s(t + 4, 3) + y) * 360 / 16777216};
    }
};
QString id(gar_subfile *sub, int offset) {
    return QString::fromLatin1(sub->mapid) + ":" + QString::number(offset);
}
} // namespace
QJsonObject garminRoadRules(gobject *object) {
    auto *poly = static_cast<gpoly *>(object->gptr);
    auto *sub = poly->subdiv->subfile;
    auto result = rules.value(sub).value(poly->lbloffset);
    result.insert("roadId", id(sub, poly->lbloffset));
    return result;
}
QJsonObject decodeGarminNodRules(const QByteArray &data, const QString &mapId, int align) {
    Bytes b{data};
    QHash<int, QJsonObject> roadRules;
    QJsonArray restrictions;
    QJsonObject report;
    try {
        if (align < 0 || align > 10 || data.isEmpty())
            throw std::runtime_error("invalid NOD alignment/data");
        int centers = 0, count = 0, restrictedRoads = 0;
        for (int start = 0; start < b.data.size();) {
            int t = b.table(start, align), format = b.u(t), na = b.u(t + 7), nb = b.u(t + 8);
            if (t <= start || (format & ~0x1f) || (format & 3) == 3)
                throw std::runtime_error("invalid NOD center");
            int a = t + 9, bt = a + na * 5, c = bt + nb * 3;
            for (int i = 0; i < na; ++i) {
                int p = a + i * 5, raw = b.u(p, 3), info = b.u(p + 3),
                    access = b.u(p + 4) | ((raw >> 8) & 0xc000);
                int net = raw & 0x3fffff;
                QJsonObject r{{"accessMask", access},         {"speedClass", info & 7},
                              {"roadClass", (info >> 4) & 7}, {"oneway", bool(info & 8)},
                              {"toll", bool(info & 128)},     {"accessDecoded", true}};
                auto &old = roadRules[net];
                if (!old.isEmpty() && old != r)
                    throw std::runtime_error("conflicting Table A road rules");
                old = r;
                if (access)
                    ++restrictedRoads;
            }
            int sizeBytes = format & 3, length = sizeBytes ? b.u(c, sizeBytes) : 0;
            int begin = c + sizeBytes, end = begin + length;
            b.u(end ? end - 1 : c);
            auto nodeRef = [&](int p) {
                int v = b.u(p, 2);
                if (v & 0x8000)
                    return t - (v & 0x7fff);
                if (v >= nb)
                    throw std::runtime_error("invalid Table B restriction reference");
                return b.u(bt + v * 3, 3);
            };
            for (int p = begin; p < end;) {
                int type = b.u(p), flags = b.u(p + 1), extra = b.u(p + 2), n = flags >> 5;
                // Unknown conditional headers must never become unrestricted roads.
                if (type != 5 || extra || (flags & 0x11) || n < 2 || n > 7)
                    throw std::runtime_error("unsupported conditional/time restriction encoding");
                int except = (flags & 2) ? 0x10 : 0;
                if (flags & 4)
                    except |= 0x8000;
                int q = p + 3;
                if (flags & 8) {
                    int v = b.u(q++);
                    if (v & ~0x77)
                        throw std::runtime_error("unsupported restriction exception flags");
                    except |= (v & 0x67) | ((v & 0x10) ? 0x4000 : 0);
                }
                QJsonArray nodes, roads;
                for (int j = 0; j <= n; ++j)
                    nodes.append(b.coordinate(nodeRef(q + j * 2), align));
                q += (n + 1) * 2;
                for (int j = 0; j < n; ++j) {
                    int index = b.u(q++);
                    if (index >= na)
                        throw std::runtime_error("invalid restriction road");
                    roads.append(mapId + ":" + QString::number(b.u(a + index * 5, 3) & 0x3fffff));
                }
                if (q > end)
                    throw std::runtime_error("truncated restriction record");
                restrictions.append(QJsonObject{{"nodes", nodes}, {"roads", roads}, {"exceptMask", except}});
                ++count;
                p = q;
            }
            int next = end + bool(format & 4) + bool(format & 8) + bool(format & 16);
            if (next <= start || next > b.data.size())
                throw std::runtime_error("invalid NOD table extent");
            start = next;
            ++centers;
        }
        report.insert("centers", centers);
        report.insert("restrictions", count);
        report.insert("roadRecords", roadRules.size());
        report.insert("restrictedRoadRecords", restrictedRoads);
        report.insert("complete", true);
    } catch (const std::exception &e) {
        report.insert("complete", false);
        report.insert("error", QString::fromUtf8(e.what()));
    }
    QJsonObject roads;
    for (auto i = roadRules.begin(); i != roadRules.end(); ++i)
        roads.insert(QString::number(i.key()), i.value());
    return {{"report", report}, {"roads", roads}, {"restrictions", restrictions}};
}

QJsonObject garminNetwork(gmap *map) {
    rules.clear();
    QJsonArray restrictions, reports;
    bool complete = true;
    for (int si = 0; si < map->lastsub; ++si) {
        auto *sub = map->subs[si];
        QJsonObject report{{"mapId", QString::fromLatin1(sub->mapid)}};
        try {
            if (!sub->net && gar_init_net(sub) < 0)
                throw std::runtime_error("NET unavailable");
            if (!sub->net || !sub->net->nod)
                throw std::runtime_error("NOD unavailable");
            const auto *nod = sub->net->nod;
            char section[] = "NOD";
            const auto headerOffset = gar_subfile_offset(sub, section);
            Bytes header;
            header.data.resize(127);
            if (glseek(sub->gimg, headerOffset, SEEK_SET) != headerOffset ||
                gread(sub->gimg, header.data.data(), 2) != 2)
                throw std::runtime_error("cannot read NOD header");
            int headerSize = header.u(0, 2);
            if (headerSize != 63 && headerSize != 127)
                throw std::runtime_error("unsupported NOD header layout");
            if (gread(sub->gimg, header.data.data() + 2, headerSize - 2) != headerSize - 2)
                throw std::runtime_error("truncated NOD header");
            if (headerSize == 127 && (header.u(0x6b, 4) || header.u(0x75, 4)))
                throw std::runtime_error("unparsed NOD extension sections may contain conditional rules");
            if (nod->cpalign > 10 || nod->roadptrsize != 5 || nod->nod1_length > 256 * 1024 * 1024)
                throw std::runtime_error("unsupported NOD table layout");
            Bytes b;
            b.data.resize(nod->nod1_length);
            const auto off = nod->nodoff + nod->nod1_offset;
            if (glseek(sub->gimg, off, SEEK_SET) != off ||
                gread(sub->gimg, b.data.data(), b.data.size()) != b.data.size())
                throw std::runtime_error("cannot read NOD1");
            auto parsed = decodeGarminNodRules(b.data, QString::fromLatin1(sub->mapid), nod->cpalign);
            report = parsed.value("report").toObject();
            report.insert("mapId", QString::fromLatin1(sub->mapid));
            if (!report.value("complete").toBool())
                throw std::runtime_error(report.value("error").toString().toStdString());
            auto roadRules = parsed.value("roads").toObject();
            for (auto i = roadRules.begin(); i != roadRules.end(); ++i)
                rules[sub].insert(i.key().toInt(), i.value().toObject());
            for (const auto &r : parsed.value("restrictions").toArray())
                restrictions.append(r);

        } catch (const std::exception &e) {
            complete = false;
            report.insert("complete", false);
            report.insert("error", QString::fromUtf8(e.what()));
        }
        reports.append(report);
    }
    return {{"restrictions", restrictions}, {"reports", reports}, {"complete", complete}};
}
