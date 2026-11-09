#pragma once
#include <QObject>
#include <QGeoCoordinate>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
class RouteService : public QObject {
    Q_OBJECT
public:
    explicit RouteService(QObject *parent = nullptr);
    ~RouteService() override;
    void plan(const QString &file, const QGeoCoordinate &start, const QGeoCoordinate &finish,
              const QString &mode, const QString &strategy);
signals:
    void ready(const QVariantMap &result);
private:
    void receive();
    void fail(const QString &message);
    QProcess process;
    QTimer timeout, idle;
    QString currentFile;
    QByteArray request, buffer;
    bool busy = false;
};
