#pragma once
#include <QFile>
#include <QGeoCoordinate>
#include <QHash>
#include <QJsonObject>
#include <QVector>
#include <memory>

struct RouteEdge {
    int destination, way;
    double distance, seconds;
    int token;
};
struct RouteNode {
    QGeoCoordinate coordinate;
    QVector<RouteEdge> edges;
};
struct RoutePrefix {
    QHash<int, int> next;
    int fail = 0;
    bool forbidden = false;
};

// Portable little-endian records, accessed in place without rebuilding Qt containers.
class GraphStorage {
  public:
    static bool write(const QString &path, const QJsonObject &metadata, const QVector<RouteNode> &nodes,
                      const QStringList &names, const QStringList &roadIds, const QVector<bool> &local,
                      const QVector<RoutePrefix> &trie, const QVector<QVector<int>> &incoming,
                      QString *error);
    static std::unique_ptr<GraphStorage> open(const QString &path, QString *error);
    ~GraphStorage();
    QJsonObject metadata;
    int nodeCount() const;
    QGeoCoordinate coordinate(int node) const;
    int edgeCount(int node) const;
    RouteEdge edge(int node, int edge) const;
    bool local(int way) const;
    QString name(int way) const;
    QString roadId(int way) const;
    int incomingCount(int node) const;
    int incoming(int node, int index) const;
    int transition(int prefix, int token) const;
    bool forbidden(int prefix) const;
    int snap(const QGeoCoordinate &point, double &distance) const;

  private:
    QFile file;
    uchar *mapping = nullptr;
    const uchar *sections[10]{};
    quint64 sizes[10]{};
    bool validate(QString *error) const;
    QString string(quint32 offset) const;
};
