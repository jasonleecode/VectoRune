#pragma once
#include <QString>
#include <QStringList>
#include <QLockFile>
#include <memory>
// Immutable, content-addressed imports. A lease pins files while a renderer uses them.
class ImportCache {
public:
    static std::shared_ptr<ImportCache> acquire(const QString &input, const QString &version,
                                              QString *error, const QString &root = {},
                                              qint64 budget = 512LL * 1024 * 1024);
    ~ImportCache();
    QString path() const { return directory; }
    bool ready() const;
    bool commit(const QStringList &files, QString *error);
    static void prune(const QString &root, qint64 budget);
private:
    QString root, directory;
    qint64 budget = 0;
    std::unique_ptr<QLockFile> lock;
};
