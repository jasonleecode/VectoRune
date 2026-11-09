#include "mapregistry.h"
#include <QFileInfo>
#include <stdexcept>
void MapRegistry::add(const QString &id, const QStringList &suffixes, Factory factory) {
    if (id.isEmpty() || suffixes.isEmpty() || !factory)
        throw std::invalid_argument("invalid map plugin registration");
    for (const auto &entry : entries) {
        if (entry.id == id) throw std::invalid_argument("duplicate map plugin id");
        for (const auto &suffix : suffixes)
            if (entry.suffixes.contains(suffix, Qt::CaseInsensitive))
                throw std::invalid_argument("ambiguous map plugin suffix");
    }
    entries.push_back({id, suffixes, std::move(factory)});
}
std::shared_ptr<MapBackend> MapRegistry::create(const QUrl &url) const {
    if (!url.isLocalFile()) return {};
    const auto suffix = QFileInfo(url.toLocalFile()).suffix();
    for (const auto &entry : entries)
        if (entry.suffixes.contains(suffix, Qt::CaseInsensitive)) return entry.factory();
    return {};
}
QStringList MapRegistry::fileFilters() const {
    QStringList all, filters;
    for (const auto &entry : entries) {
        QStringList patterns;
        for (const auto &suffix : entry.suffixes) patterns << "*." + suffix;
        all.append(patterns);
        filters << entry.id + " (" + patterns.join(' ') + ")";
    }
    filters.prepend(QObject::tr("地图文件 (%1)").arg(all.join(' ')));
    return filters;
}
