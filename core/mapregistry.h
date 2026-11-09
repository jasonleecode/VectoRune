#pragma once
#include "mapbackend.h"
#include <functional>
#include <memory>
#include <vector>

class MapRegistry {
public:
    using Factory = std::function<std::shared_ptr<MapBackend>()>;
    void add(const QString &id, const QStringList &suffixes, Factory factory);
    std::shared_ptr<MapBackend> create(const QUrl &url) const;
    QStringList fileFilters() const;
private:
    struct Entry { QString id; QStringList suffixes; Factory factory; };
    std::vector<Entry> entries;
};
