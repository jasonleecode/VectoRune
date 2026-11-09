#pragma once
#include "mapbackend.h"
#include <QTemporaryFile>
#include <QProcess>
#include <memory>
#include <vector>
class MbtilesBackend final : public MapBackend {
public:
    explicit MbtilesBackend(QObject *parent = nullptr);
    ~MbtilesBackend() override { stopWorkers(); }
    QVariantMap prepare(const QUrl &url) override;
    void searchPlaces(const QString &query) override;
private:
    std::vector<std::unique_ptr<QTemporaryFile>> styles;
    QString searchMapFile;
    quint64 searchGeneration = 0;
    QProcess *searchProcess = nullptr;
};
