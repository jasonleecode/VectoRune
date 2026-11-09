#include "runtimepaths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
QString RuntimePaths::cacheDirectory() {
    const auto configured = qEnvironmentVariable("VECTORUNE_CACHE_DIR");
    return configured.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                : QDir(configured).absolutePath();
}
QString RuntimePaths::helper(const QString &name) {
    const auto folder = QCoreApplication::applicationDirPath();
    const QStringList paths{qEnvironmentVariable("VECTORUNE_HELPER_DIR"), folder,
                            QDir(folder).filePath("../libexec/vectorune")};
    for (const auto &path : paths)
        if (!path.isEmpty() && QFileInfo(QDir(path).filePath(name)).isExecutable())
            return QDir(path).filePath(name);
    return QDir(folder).filePath(name); // QProcess reports a missing helper explicitly.
}
QString RuntimePaths::dataDirectory() {
    const auto configured = qEnvironmentVariable("VECTORUNE_DATA_DIR");
    if (!configured.isEmpty()) return QDir(configured).absolutePath();
    const QStringList candidates{
        QDir::current().filePath("data"),
        QDir(QCoreApplication::applicationDirPath()).filePath("../share/vectorune"),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)};
    for (const auto &path : candidates)
        if (QFileInfo(QDir(path).filePath("maps")).isDir()) return path;
    return candidates.last();
}
