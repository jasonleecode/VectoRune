#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <memory>
class GeometryIndex {
  public:
    GeometryIndex(const QString &path, bool create);
    ~GeometryIndex();
    void add(const QJsonObject &feature);
    void commit();
    QByteArray tile(int zoom, int x, int y, int *featuresRead = nullptr) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
