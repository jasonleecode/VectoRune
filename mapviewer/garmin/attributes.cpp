#include "attributes.h"
#include <QJsonArray>
#include <QString>
#include <iconv.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
extern "C" {
#include "libgarmin.h"
#include "libgarmin_priv.h"
#include "garmin_rgn.h"
#include "garmin_lbl.h"
}
#undef log

QJsonObject garminRoutingInfo(gobject *object)
{
    if (object->type != GO_POLYLINE) return {};
    auto *poly = static_cast<gpoly *>(object->gptr);
    if (!poly->netlbl || !poly->subdiv->subfile->have_net) return {};
    QJsonArray junctions;
    if (poly->nodemap) {
        for (int i = 0; i <= poly->npoints; ++i)
            if (poly->nodemap[i / 8] & (1u << (i % 8))) junctions.append(i);
    }
    return {{"junctions", junctions}, {"hasJunctionFlags", poly->nodemap != nullptr}};
}

static QString text(const char *raw, const char *encoding)
{
    if (!raw || !*raw) return {};
    auto converter = iconv_open("UTF-8", encoding);
    if (converter == reinterpret_cast<iconv_t>(-1)) return {};
    QByteArray result(strlen(raw) * 4 + 16, '\0');
    char *input = const_cast<char *>(raw), *output = result.data();
    size_t inSize = strlen(raw), outSize = result.size();
    const auto status = iconv(converter, &input, &inSize, &output, &outSize);
    iconv_close(converter);
    if (status == size_t(-1)) return {};
    result.resize(result.size() - outSize);
    QString decoded = QString::fromUtf8(result);
    for (auto i = decoded.size(); i > 0; --i)
        if (decoded[i-1].unicode() < 32) decoded.remove(i-1, 1);
    while (decoded.startsWith('@')) decoded.remove(0, 1);
    return decoded.trimmed();
}

static QString cityName(gar_subfile *sub, int index)
{
    if (index <= 0 || index >= sub->cicount || !sub->cities || !sub->cities[index]) return {};
    auto *city = sub->cities[index];
    if (city->label) return text(city->label, sub->lbl->codepage);
    auto *object = gar_get_subfile_object_byidx(sub, city->subdiv_idx, city->point_idx, GO_POINT);
    if (!object) return {};
    char *raw = gar_get_object_lbl(object);
    const auto name = text(raw, sub->lbl->codepage);
    free(raw);
    gar_free_objects(object);
    return name;
}

QJsonObject garminMetadata(gmap *map)
{
    QJsonArray submaps;
    for (int i = 0; i < map->lastsub; ++i) {
        auto *sub = map->subs[i];
        if (!sub->countries) gar_init_srch(sub, 0);
        if (!sub->cities) gar_init_srch(sub, 1);
        submaps.append(QJsonObject{{"mapId", QString::fromLatin1(sub->mapid)},
            {"encoding", sub->lbl ? sub->lbl->codepage : ""},
            {"countries", sub->lbl_countries}, {"regionRecords", sub->lbl_regions},
            {"cityRecords", sub->lbl_cities}, {"zipRecords", sub->lbl_zips},
            {"hasNET", bool(sub->have_net)}, {"hasNOD", bool(sub->have_nod)}});
    }
    return {{"submaps", submaps}};
}

QJsonObject garminAttributes(gobject *object)
{
    QJsonObject result;
    auto *sub = object->type == GO_POINT ? static_cast<gpoint *>(object->gptr)->subdiv->subfile
                                       : static_cast<gpoly *>(object->gptr)->subdiv->subfile;
    char *raw = gar_get_object_lbl(object);
    const auto name = text(raw, gar_obj_codepage(object));
    free(raw);
    if (!name.isEmpty()) result.insert("name", name);
    const int type = gar_obj_type(object);
    result.insert("garmin_type", type);
    result.insert("garmin_type_hex", "0x" + QString::number(type, 16));
    result.insert("map_id", QString::fromLatin1(sub->mapid));
    if (object->type == GO_POLYLINE) {
        const int flags = gar_object_flags(object);
        result.insert("oneway", bool(flags & F_ONEWAY));
        result.insert("segmented", bool(flags & F_SEGMENTED));
        return result;
    }
    if (object->type != GO_POINT) return result;
    QString kind = "其他地点";
    switch (type >> 8) {
    case 0x2a: kind = "餐饮"; break;
    case 0x2b: kind = "住宿"; break;
    case 0x2c: kind = "文化/体育/景点"; break;
    case 0x2d: kind = "休闲娱乐"; break;
    case 0x2e: kind = "购物"; break;
    case 0x2f: kind = "交通/生活服务"; break;
    case 0x30: kind = "公共服务"; break;
    }
    if (type == 0x2f01) kind = "加油站";
    if (type == 0x2f0b) kind = "停车场";
    if (type == 0x4e00) kind = "公共卫生间";
    result.insert("category", kind);
    auto *properties = gar_get_poi_properties(static_cast<gpoint *>(object->gptr));
    if (!properties) return result;
    const auto encoding = sub->lbl->codepage;
    auto put = [&](const QString &key, const QString &value) { if (!value.isEmpty()) result.insert(key, value); };
    put("house_number", text(properties->number, encoding));
    put("phone", text(properties->phone, encoding));
    unsigned char buffer[2048];
    if (properties->streetoff && gar_get_lbl(sub, properties->streetoff, L_LBL, buffer, sizeof(buffer)) > 0)
        put("street", text(reinterpret_cast<char *>(buffer), encoding));
    put("city", cityName(sub, properties->cityidx));
    if (properties->cityidx > 0 && properties->cityidx < sub->cicount && sub->cities[properties->cityidx]) {
        const auto regionIndex = sub->cities[properties->cityidx]->region_idx;
        if (regionIndex > 0 && regionIndex < sub->rcount && sub->regions[regionIndex]) {
            auto *region = sub->regions[regionIndex];
            // Some map styles repurpose the region field (e.g. opening hours).
            put("region_raw", text(region->name, encoding));
            if (region->country > 0 && region->country < sub->ccount)
                put("country", text(sub->countries[region->country], encoding));
        }
    }
    if (properties->zipidx > 0 && properties->zipidx < sub->czips && sub->zips[properties->zipidx])
        put("postcode_raw", text(sub->zips[properties->zipidx]->code, encoding));
    gar_free_poi_properties(properties);
    return result;
}
