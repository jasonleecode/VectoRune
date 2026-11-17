#pragma once
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QFutureWatcher>
#include <QUrl>
#include <QVariant>
#include <memory>

class MapConverter : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString log READ log NOTIFY changed)
    Q_PROPERTY(QUrl result READ result NOTIFY changed)
    Q_PROPERTY(bool canOpen READ canOpen NOTIFY changed)
    Q_PROPERTY(QVariantList elements READ elements CONSTANT)
public:
    explicit MapConverter(QObject *parent = nullptr);
    ~MapConverter() override;
    bool available() const;
    bool running() const { return busy; }
    QString status() const { return message; }
    QString log() const { return outputLog; }
    QUrl result() const { return resultUrl; }
    bool canOpen() const { return !resultUrl.isEmpty() && format == "mbtiles"; }
    QVariantList elements() const;
    Q_INVOKABLE bool start(const QUrl &input, const QUrl &output, const QStringList &groups,
                           const QString &preset, const QString &format, int minZoom, int maxZoom);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QUrl suggestedOutput(const QUrl &input, const QString &format) const;
    Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }
    Q_INVOKABLE QUrl fileUrl(const QString &path) const { return QUrl::fromLocalFile(path); }
signals:
    void changed();
private:
    void readOutput();
    void finish(int code, QProcess::ExitStatus status);
    void fail(const QString &error);
    QString validateOutput() const;
    bool writeStyle();
    void publishOutput();
    QProcess process;
    QFutureWatcher<QString> validation;
    QTimer killTimer;
    std::unique_ptr<QTemporaryDir> temporary;
    bool busy = false, cancelled = false;
    QString message, outputLog, destination, stagedOutput, format, preset;
    QStringList selectedLayers;
    QUrl resultUrl;
};
