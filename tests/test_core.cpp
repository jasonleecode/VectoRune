#include "mapdocument.h"
#include "importcache.h"
#include "placeindex.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>

static void check(bool condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void write(const QString &file, const QByteArray &bytes) {
    QFile f(file); check(f.open(QIODevice::WriteOnly), "fixture open");
    check(f.write(bytes) == bytes.size(), "fixture write");
}
class Fake : public MapBackend {
public:
    QVariantMap prepare(const QUrl &url) override {
        return url.toLocalFile().contains("bad") ? QVariantMap{{"error", "failed"}} : QVariantMap{{"name", "fake"}};
    }
    void searchPlaces(const QString &query) override { emit placesReady({{"query", query}}); }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    MapRegistry registry;
    std::weak_ptr<MapBackend> latest;
    registry.add("fake", {"fixture"}, [&] {
        auto value = std::make_shared<Fake>(); latest = value; return value;
    });
    bool rejected = false;
    try { registry.add("second", {"FIXTURE"}, [] { return std::make_shared<Fake>(); }); }
    catch (const std::invalid_argument &) { rejected = true; }
    check(rejected, "ambiguous registration rejected");
    check(!registry.create(QUrl("https://example.test/map.fixture")), "remote URLs rejected");
    MapDocument document(std::move(registry));
    auto url = [](const char *name) { return QUrl::fromLocalFile(QString("/tmp/") + name); };
    check(!document.prepare(url("first.FIXTURE")).contains("error"), "case insensitive selection");
    auto first = latest;
    auto renderer = std::make_unique<QObject>();
    document.retainForRenderer(renderer.get());
    document.load(url("bad.fixture"));
    check(!first.expired(), "failed load preserves current session");
    QString reply;
    QObject::connect(&document, &MapDocument::placesReady, [&reply](const QVariantMap &r) { reply = r.value("query").toString(); });
    document.load(url("second.fixture"));
    check(!first.expired(), "renderer pins old session across switch");
    first.lock()->searchPlaces("stale");
    check(reply.isEmpty(), "old session results must not escape");
    document.searchPlaces("current");
    check(reply == "current", "current session dispatch");
    renderer.reset();
    check(first.expired(), "renderer teardown releases old data");

    QTemporaryDir temporary;
    const auto source = temporary.filePath("source.img"), root = temporary.filePath("cache");
    write(source, "one");
    QString error;
    auto cache = ImportCache::acquire(source, "v1", &error, root, 1024);
    check(bool(cache) && !cache->ready(), "first import requires build");
    write(cache->path() + "/data", QByteArray(700, 'a'));
    check(cache->commit({"data"}, &error), "publish complete import");
    const auto oldPath = cache->path();
    auto shared = ImportCache::acquire(source, "v1", &error, root, 1024);
    check(shared == cache && shared->ready(), "same content reuses pinned lease");
    shared.reset();
    write(source, "two");
    auto next = ImportCache::acquire(source, "v1", &error, root, 1024);
    check(next->path() != oldPath, "source change invalidates cache");
    write(next->path() + "/data", QByteArray(700, 'b'));
    check(next->commit({"data"}, &error), "second pinned import");
    check(QFile::exists(oldPath + "/data"), "quota eviction never removes live renderer files");
    cache.reset();
    check(!QFile::exists(oldPath + "/data"), "unpin applies disk budget");
    const auto nextPath = next->path(); next.reset();
    auto reused = ImportCache::acquire(source, "v1", &error, root, 1024);
    check(reused->ready() && reused->path() == nextPath, "reuse across sessions");
    reused.reset();
    write(nextPath + "/data", "truncated");
    auto damaged = ImportCache::acquire(source, "v1", &error, root, 1024);
    check(!damaged->ready(), "incomplete cache rebuilds");
    damaged.reset();
    check(!QFile::exists(nextPath), "aborted imports cleaned up");
    const auto indexFile = temporary.filePath("places.sqlite");
    {
        PlaceIndex index(indexFile, true);
        for (int i = 0; i < 60; ++i)
            index.add({{"name", QString("Café %1").arg(i)}, {"latitude", 40}, {"longitude", 116}}, QString::number(i));
        index.add({{"name", "Café"}, {"latitude", 40}, {"longitude", 116}}, "exact");
        index.add({{"name", "跨日期线地点"}, {"latitude", 0}, {"longitude", -179.999}}, "dateline");
        index.commit();
    }
    PlaceIndex index(indexFile, false);
    const auto matches = index.search("cafe");
    check(matches.size() == 50 && matches.first().toObject().value("name") == "Café", "global exact match before limit");
    check(index.search("afe").size() == 50 && index.search("跨日期").size() == 1, "substring/Chinese indexed search");
    check(index.search("no-match").isEmpty(), "no false matches");
    check(index.nearby(QGeoCoordinate(0, 179.999), 1000).value("name") == "跨日期线地点", "spatial query wraps dateline");
    check(index.nearby(QGeoCoordinate(0, 179.999), 10).contains("error"), "nearby query enforces exact radius");
    puts("PASS: registration, transactional switch, renderer leases, cache reuse/invalidation/quota/abort");
}
