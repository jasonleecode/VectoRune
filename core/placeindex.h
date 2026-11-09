#pragma once
#include <QGeoCoordinate>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <memory>
// Disk-backed name bigrams and R-tree. Payloads are read only for matching places.
class PlaceIndex {
public:
    PlaceIndex(const QString &path, bool create);
    ~PlaceIndex();
    void add(const QJsonObject &place, const QString &identity);
    void commit();
    QJsonArray search(const QString &term, int limit = 50) const;
    QJsonObject nearby(const QGeoCoordinate &coordinate, double radius) const;
    static QString folded(const QString &text);
private:
    QString connection;
    QSqlDatabase database;
    std::unique_ptr<QSqlQuery> insertPlace, insertGram, insertPosition;
};
