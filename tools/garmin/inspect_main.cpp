#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <iconv.h>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
extern "C" {
#include "libgarmin.h"
#include "libgarmin_priv.h"
#include "garmin_rgn.h"
#include "garmin_lbl.h"
#include "garmin_fat.h"
}
#undef log

static void quietLog(char *, int, int, char *, ...) {}
static QString decode(const char *raw, int codepage)
{
    if (!raw || !*raw) return {};
    QByteArray encoding = codepage == 65001 ? "UTF-8" : "CP" + QByteArray::number(codepage);
    if (codepage == 0) encoding = "ASCII";
    iconv_t converter = iconv_open("UTF-8", encoding.constData());
    QByteArray output(strlen(raw) * 4 + 16, '\0');
    char *input = const_cast<char *>(raw), *destination = output.data();
    size_t inSize = strlen(raw), outSize = output.size();
    if (converter != reinterpret_cast<iconv_t>(-1)) {
        iconv(converter, &input, &inSize, &destination, &outSize);
        iconv_close(converter);
        output.resize(output.size() - outSize);
    } else output = QByteArray(raw);
    QString text = QString::fromUtf8(output);
    for (qsizetype i = text.size(); i > 0; --i)
        if (text[i-1].unicode() < 32) text.remove(i-1, 1);
    return text.trimmed();
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 3) return 1;
    gar_config config{}; config.opm = OPM_GPS;
    auto *garmin = gar_init_cfg(nullptr, quietLog, &config);
    if (!garmin || gar_img_load(garmin, argv[1], 1) <= 0) return 2;
    auto *map = gar_find_subfiles(garmin, nullptr, 0);
    if (!map) return 2;
    gobject *objects = nullptr;
    if (gar_get_objects(map, 24, nullptr, &objects, 0) <= 0) return 2;

    QMap<gar_subfile *, int> codepages;
    QJsonArray submaps;
    for (int i = 0; i < map->lastsub; ++i) {
        auto *sub = map->subs[i];
        hdr_lbl_t header{};
        char extension[] = "LBL";
        auto offset = gar_subfile_offset(sub, extension);
        glseek(sub->gimg, offset, SEEK_SET);
        gread(sub->gimg, &header, sizeof(header));
        codepages[sub] = header.codepage;
        gar_init_srch(sub, 0);
        gar_init_srch(sub, 1);
        QJsonArray countries, regions, cities, zips;
        for (int n = 1; n < sub->ccount; ++n)
            if (sub->countries[n]) countries.append(decode(sub->countries[n], header.codepage));
        for (int n = 1; n < sub->rcount && regions.size() < 10; ++n)
            if (sub->regions[n]) regions.append(decode(sub->regions[n]->name, header.codepage));
        for (int n = 1; n < sub->cicount && cities.size() < 10; ++n)
            if (sub->cities[n] && sub->cities[n]->label)
                cities.append(decode(sub->cities[n]->label, header.codepage));
        for (int n = 1; n < sub->czips && zips.size() < 10; ++n)
            if (sub->zips[n]) zips.append(decode(sub->zips[n]->code, header.codepage));
        submaps.append(QJsonObject{{"mapId", QString::fromLatin1(sub->mapid)},
            {"codepage", header.codepage}, {"libraryEncoding", sub->lbl->codepage},
            {"labelCoding", header.coding}, {"net", bool(sub->have_net)}, {"nod", bool(sub->have_nod)},
            {"countries", sub->lbl_countries}, {"regions", sub->lbl_regions},
            {"cities", sub->lbl_cities}, {"zips", sub->lbl_zips},
            {"countrySamples", countries}, {"regionSamples", regions},
            {"citySamples", cities}, {"zipSamples", zips}});
    }

    QJsonObject counts, histogram, samples, propertyCounts;
    QSet<QString> distinctRoads, distinctPoi, distinctAreas;
    for (auto *object = objects; object; object = object->next) {
        const QString group = object->type == GO_POINT ? "points"
                            : object->type == GO_POLYLINE ? "lines" : "polygons";
        auto *sub = object->type == GO_POINT ? static_cast<gpoint *>(object->gptr)->subdiv->subfile
                                           : static_cast<gpoly *>(object->gptr)->subdiv->subfile;
        const int type = gar_obj_type(object);
        const QString key = group + ":0x" + QString::number(type, 16);
        histogram[key] = histogram.value(key).toInt() + 1;
        counts[group] = counts.value(group).toInt() + 1;
        char *raw = gar_get_object_lbl(object);
        const auto name = decode(raw, codepages.value(sub));
        free(raw);
        if (!name.isEmpty()) {
            const QString named = group + "WithName";
            counts[named] = counts.value(named).toInt() + 1;
            auto examples = samples.value(group).toArray();
            if (examples.size() < 20)
                examples.append(QJsonObject{{"name", name}, {"type", key}});
            samples[group] = examples;
            if (object->type == GO_POINT) distinctPoi.insert(name);
            else if (object->type == GO_POLYLINE) distinctRoads.insert(name);
            else distinctAreas.insert(name);
        }
        if (object->type == GO_POLYLINE) {
            const auto flags = gar_object_flags(object);
            if (flags & F_ONEWAY) counts["onewayLines"] = counts.value("onewayLines").toInt() + 1;
            if (flags & F_SEGMENTED) counts["segmentedLines"] = counts.value("segmentedLines").toInt() + 1;
            if (flags & F_LOCKONROAD) counts["lockOnRoadLines"] = counts.value("lockOnRoadLines").toInt() + 1;
        } else if (object->type == GO_POINT) {
            auto *properties = gar_get_poi_properties(static_cast<gpoint *>(object->gptr));
            if (!properties) continue;
            auto increment = [&](const QString &key, bool present) {
                if (present) propertyCounts[key] = propertyCounts.value(key).toInt() + 1;
            };
            increment("poiRecords", true);
            increment("houseNumber", properties->number && *properties->number);
            increment("street", properties->streetoff != 0);
            increment("city", properties->cityidx != 0);
            increment("zip", properties->zipidx != 0);
            increment("phone", properties->phone && *properties->phone);
            increment("exit", properties->exitoff != 0);
            if (properties->phone && *properties->phone) {
                auto examples = samples.value("poiDetails").toArray();
                if (examples.size() < 8)
                    examples.append(QJsonObject{{"name", name}, {"phone", QString::fromLatin1(properties->phone)},
                        {"number", decode(properties->number, codepages.value(sub))},
                        {"cityIndex", properties->cityidx}, {"zipIndex", properties->zipidx}});
                samples["poiDetails"] = examples;
            }
            gar_free_poi_properties(properties);
        }
    }
    counts["distinctLineNames"] = distinctRoads.size();
    counts["distinctPointNames"] = distinctPoi.size();
    counts["distinctPolygonNames"] = distinctAreas.size();
    QJsonArray sections;
    fat_entry *entry;
    list_for_entry(entry, &map->subs[0]->gimg->lfatfiles, l) {
        sections.append(QJsonObject{{"name", QString::fromLatin1(entry->filename)},
                                    {"bytes", double(entry->size)}});
    }
    QJsonObject report{{"sections", sections}, {"submaps", submaps}, {"counts", counts}, {"poiProperties", propertyCounts},
        {"typeHistogram", histogram}, {"samples", samples}};
    QFile output(QString::fromLocal8Bit(argv[2]));
    if (!output.open(QIODevice::WriteOnly)) return 3;
    output.write(QJsonDocument(report).toJson());
    return 0;
}
