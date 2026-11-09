#include "builtin.h"
#include "mapdocument.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <cstdio>
static void check(bool condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    const auto path = temporary.filePath("fixture.mbtiles");
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE"); db.setDatabaseName(path);
        check(db.open(), "fixture database opens"); QSqlQuery q(db);
        check(q.exec("CREATE TABLE metadata(name,value)"), "metadata table");
        check(q.exec("INSERT INTO metadata VALUES('format','png'),('center','12,34,8')"), "metadata values");
        check(q.exec("CREATE TABLE tiles(zoom_level,tile_column,tile_row,tile_data)"), "tile table");
        check(q.exec("INSERT INTO tiles VALUES(0,0,0,X'00')"), "tile fixture");
    }
    QSqlDatabase::removeDatabase(QSqlDatabase::defaultConnection);
    MapDocument document(builtinMaps());
    auto raster = document.prepare(QUrl::fromLocalFile(path));
    check(!raster.contains("error"), "raster adapter prepares");
    const auto capabilities = raster.value("capabilities").toMap();
    check(capabilities.value("tiles").toBool() && !capabilities.value("search").toBool()
          && !capabilities.value("routing").toBool(), "raster capabilities are accurate");
    check(raster.value("latitude").toDouble() == 34 && raster.value("longitude").toDouble() == 12, "map center metadata");
    const auto style = QUrl(raster.value("styleUrl").toString()).toLocalFile();
    auto renderer = std::make_unique<QObject>(); document.retainForRenderer(renderer.get());
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE"); db.setDatabaseName(path); db.open(); QSqlQuery q(db);
        check(q.exec("UPDATE metadata SET value='pbf' WHERE name='format'"), "vector format");
        check(q.exec("INSERT INTO metadata VALUES('json','{\"vector_layers\":[{\"id\":\"transportation\"}]}')"), "vector layers");
    }
    QSqlDatabase::removeDatabase(QSqlDatabase::defaultConnection);
    const auto vector = document.prepare(QUrl::fromLocalFile(path));
    check(!vector.contains("error") && vector.value("capabilities").toMap().value("search").toBool(), "vector style resource and capabilities");
    check(QFileInfo::exists(style), "retired style survives while renderer exists");
    renderer.reset(); check(!QFileInfo::exists(style), "retired style is removed on renderer teardown");
    check(document.prepare(QUrl::fromLocalFile(temporary.filePath("unknown.xyz"))).contains("error"), "unregistered format reports error");
    puts("PASS: compiled plugins, raster/vector capabilities, metadata, embedded style, renderer lifetime");
}
