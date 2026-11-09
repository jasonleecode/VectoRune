#include "placeindex.h"
#include <QJsonDocument>
#include <QSet>
#include <QSqlError>
#include <QUuid>
#include <QVariant>
#include <cmath>
#include <stdexcept>

namespace {
void check(bool success, const QString &message) {
    if (!success) throw std::runtime_error(message.toStdString());
}
QString firstGram(const QString &text) {
    const auto codepoints = text.toUcs4();
    return codepoints.size() < 2 ? QString() : QString::fromUcs4(codepoints.data(), 2);
}
}
QString PlaceIndex::folded(const QString &text) {
    QString result;
    for (auto c : text.normalized(QString::NormalizationForm_D).toCaseFolded())
        if (c.category() != QChar::Mark_NonSpacing) result += c;
    return result.trimmed();
}
PlaceIndex::PlaceIndex(const QString &path, bool create) {
    connection = QUuid::createUuid().toString();
    database = QSqlDatabase::addDatabase("QSQLITE", connection);
    database.setDatabaseName(path);
    if (!create) database.setConnectOptions("QSQLITE_OPEN_READONLY");
    try {
        check(database.open(), "无法打开地点索引。");
        QSqlQuery q(database);
        check(q.exec("PRAGMA cache_size=-4096"), q.lastError().text());
        if (!create) {
            check(q.exec("SELECT id FROM places LIMIT 1"), "地点索引损坏。");
            return;
        }
        check(q.exec("CREATE TABLE places(id INTEGER PRIMARY KEY, identity TEXT UNIQUE, key TEXT, payload BLOB)"), q.lastError().text());
        check(q.exec("CREATE INDEX names ON places(key)"), q.lastError().text());
        check(q.exec("CREATE TABLE grams(gram TEXT, place INTEGER, PRIMARY KEY(gram,place)) WITHOUT ROWID"), q.lastError().text());
        check(q.exec("CREATE VIRTUAL TABLE positions USING rtree(id,west,east,south,north)"), q.lastError().text());
        check(database.transaction(), "无法创建地点索引事务。");
        insertPlace = std::make_unique<QSqlQuery>(database);
        insertGram = std::make_unique<QSqlQuery>(database);
        insertPosition = std::make_unique<QSqlQuery>(database);
        check(insertPlace->prepare("INSERT OR IGNORE INTO places(identity,key,payload) VALUES(?,?,?)"), "地点索引语句无效。");
        check(insertGram->prepare("INSERT INTO grams VALUES(?,?)"), "名称索引语句无效。");
        check(insertPosition->prepare("INSERT INTO positions VALUES(?,?,?,?,?)"), "空间索引语句无效。");
    } catch (...) {
        insertPlace.reset(); insertGram.reset(); insertPosition.reset();
        database = {}; QSqlDatabase::removeDatabase(connection); throw;
    }
}
PlaceIndex::~PlaceIndex() {
    insertPlace.reset(); insertGram.reset(); insertPosition.reset();
    database = {}; QSqlDatabase::removeDatabase(connection);
}
void PlaceIndex::add(const QJsonObject &place, const QString &identity) {
    const auto key = folded(place.value("name").toString());
    const QGeoCoordinate point(place.value("latitude").toDouble(), place.value("longitude").toDouble());
    if (!point.isValid()) return;
    insertPlace->bindValue(0, identity); insertPlace->bindValue(1, key);
    insertPlace->bindValue(2, QJsonDocument(place).toJson(QJsonDocument::Compact));
    check(insertPlace->exec(), insertPlace->lastError().text());
    if (!insertPlace->numRowsAffected()) return;
    const auto id = insertPlace->lastInsertId();
    const auto letters = key.toUcs4();
    QSet<QString> grams;
    for (int i = 0; i + 1 < letters.size(); ++i) grams.insert(QString::fromUcs4(letters.data() + i, 2));
    for (const auto &gram : grams) {
        insertGram->bindValue(0, gram); insertGram->bindValue(1, id);
        check(insertGram->exec(), insertGram->lastError().text());
    }
    insertPosition->bindValue(0, id);
    insertPosition->bindValue(1, point.longitude()); insertPosition->bindValue(2, point.longitude());
    insertPosition->bindValue(3, point.latitude()); insertPosition->bindValue(4, point.latitude());
    check(insertPosition->exec(), insertPosition->lastError().text());
}
void PlaceIndex::commit() { check(database.commit(), "无法完成地点索引事务。"); }
QJsonArray PlaceIndex::search(const QString &query, int limit) const {
    QJsonArray results;
    const auto term = folded(query), gram = firstGram(term);
    if (term.isEmpty()) return results;
    QSqlQuery q(database);
    const auto join = gram.isEmpty() ? QString() : "JOIN grams g ON g.place=p.id AND g.gram=:gram ";
    check(q.prepare("SELECT p.payload FROM places p " + join +
        "WHERE instr(p.key,:term)>0 ORDER BY CASE WHEN p.key=:term THEN 0 "
        "WHEN substr(p.key,1,length(:term))=:term THEN 1 ELSE 2 END,p.key,p.id LIMIT :limit"), q.lastError().text());
    if (!gram.isEmpty()) q.bindValue(":gram", gram);
    q.bindValue(":term", term); q.bindValue(":limit", qBound(1, limit, 200));
    check(q.exec(), q.lastError().text());
    while (q.next()) results.append(QJsonDocument::fromJson(q.value(0).toByteArray()).object());
    return results;
}
QJsonObject PlaceIndex::nearby(const QGeoCoordinate &coordinate, double radius) const {
    if (!coordinate.isValid() || !std::isfinite(radius) || radius <= 0)
        return {{"error", "查询位置无效。"}};
    radius = qMin(radius, 10000.0);
    const double degrees = radius / 6371000.0 * 180.0 / M_PI * 1.01;
    const double south = qMax(-90.0, coordinate.latitude() - degrees);
    const double north = qMin(90.0, coordinate.latitude() + degrees);
    const double longitudeRange = qMin(180.0, degrees / qMax(0.000001,
        std::cos(qMax(std::abs(south), std::abs(north)) * M_PI / 180.0)));
    const double west = coordinate.longitude() - longitudeRange, east = coordinate.longitude() + longitudeRange;
    QString longitude = "r.east>=:west AND r.west<=:east";
    if (west < -180 || east > 180) longitude = "(r.east>=:west OR r.west<=:east)";
    QSqlQuery q(database);
    check(q.prepare("SELECT p.payload FROM positions r JOIN places p ON p.id=r.id WHERE " + longitude +
        " AND r.north>=:south AND r.south<=:north"), q.lastError().text());
    q.bindValue(":west", west < -180 ? west + 360 : west);
    q.bindValue(":east", east > 180 ? east - 360 : east);
    q.bindValue(":south", south); q.bindValue(":north", north);
    check(q.exec(), q.lastError().text());
    QJsonObject best;
    while (q.next()) {
        const auto value = QJsonDocument::fromJson(q.value(0).toByteArray()).object();
        const double distance = coordinate.distanceTo(QGeoCoordinate(value.value("latitude").toDouble(), value.value("longitude").toDouble()));
        if (distance <= radius) { radius = distance; best = value; best.insert("distance", distance); }
    }
    return best.isEmpty() ? QJsonObject{{"error", "附近没有地点，请靠近 POI 点后重试。"}} : best;
}
