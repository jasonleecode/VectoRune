#include "builtin.h"
#include "mapdocument.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
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
          && !capabilities.value("routing").toBool() && !capabilities.value("buildings3d").toBool(), "raster capabilities are accurate");
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
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE"); db.setDatabaseName(path); db.open(); QSqlQuery q(db);
        check(q.exec("UPDATE metadata SET value='{\"vector_layers\":[{\"id\":\"building\"}]}' WHERE name='json'"), "building-only metadata");
        check(q.exec("INSERT INTO metadata VALUES('vectorune:style','dark')"), "style preset metadata");
    }
    QSqlDatabase::removeDatabase(QSqlDatabase::defaultConnection);
    const auto buildings = document.prepare(QUrl::fromLocalFile(path));
    check(!buildings.contains("error"), "selected-layer export opens without transportation");
    check(!buildings.value("capabilities").toMap().value("buildings3d").toBool(), "buildings without height do not advertise 3D");
    QFile styled(QUrl(buildings.value("styleUrl").toString()).toLocalFile());
    check(styled.open(QIODevice::ReadOnly), "converted style opens");
    const auto background = QJsonDocument::fromJson(styled.readAll()).object()["layers"].toArray()[0].toObject()["paint"].toObject()["background-color"].toString();
    check(background == "#202630", "saved dark preset is applied when reopening MBTiles");
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE"); db.setDatabaseName(path); db.open(); QSqlQuery q(db);
        check(q.exec("UPDATE metadata SET value='{\"vector_layers\":[{\"id\":\"building\",\"fields\":{\"render_height\":\"Number\"}}]}' WHERE name='json'"), "building heights metadata");
    }
    QSqlDatabase::removeDatabase(QSqlDatabase::defaultConnection);
    const auto tallBuildings = document.prepare(QUrl::fromLocalFile(path));
    check(tallBuildings.value("capabilities").toMap().value("buildings3d").toBool(), "building heights enable 3D");
    QFile extruded(QUrl(tallBuildings.value("styleUrl").toString()).toLocalFile());
    check(extruded.open(QIODevice::ReadOnly), "3D style opens");
    bool extrusionFound = false;
    for (const auto &layer : QJsonDocument::fromJson(extruded.readAll()).object()["layers"].toArray()) {
        const auto object = layer.toObject();
        if (object["id"] != "buildings-3d") continue;
        extrusionFound = true;
        check(object["type"] == "fill-extrusion" && object["layout"].toObject()["visibility"] == "none", "3D starts hidden");
        check(object["paint"].toObject()["fill-extrusion-color"] == "#535a65", "3D follows the saved palette");
    }
    check(extrusionFound, "3D building layer is included");
    check(document.prepare(QUrl::fromLocalFile(temporary.filePath("unknown.xyz"))).contains("error"), "unregistered format reports error");
    puts("PASS: compiled plugins, raster/vector capabilities, metadata, embedded style, renderer lifetime");
}
