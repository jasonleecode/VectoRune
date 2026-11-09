#include "importcache.h"
#include "runtimepaths.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace {
qint64 sizeOf(const QString &directory) {
    qint64 size = 0;
    QDirIterator it(directory, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); size += it.fileInfo().size(); }
    return size;
}
// Accessed on the document/UI thread; no background parser enters this registry.
QHash<QString, std::weak_ptr<ImportCache>> leases;
}
std::shared_ptr<ImportCache> ImportCache::acquire(const QString &input, const QString &version,
                                               QString *error, const QString &configuredRoot, qint64 budget) {
    auto fail = [&](const QString &message) -> std::shared_ptr<ImportCache> { *error = message; return {}; };
    QFile source(input);
    if (!source.open(QIODevice::ReadOnly)) return fail("无法读取地图以检查缓存。");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(version.toUtf8());
    if (!hash.addData(&source)) return fail("无法计算地图缓存标识。");
    const auto root = configuredRoot.isEmpty()
        ? RuntimePaths::cacheDirectory() + "/imports" : configuredRoot;
    if (!QDir().mkpath(root)) return fail("无法创建地图缓存目录。");
    const auto key = QString::fromLatin1(hash.result().toHex());
    const auto directory = QDir(root).filePath(key);
    if (auto existing = leases.value(directory).lock()) {
        if (!existing->ready()) return fail("该地图正在转换，请稍后重试。");
        return existing;
    }
    auto cache = std::shared_ptr<ImportCache>(new ImportCache);
    cache->root = root; cache->directory = directory; cache->budget = budget;
    cache->lock = std::make_unique<QLockFile>(directory + ".lock");
    cache->lock->setStaleLockTime(0);
    if (!cache->lock->tryLock()) {
        cache->directory.clear();
        return fail("另一进程正在使用该地图缓存，请稍后重试。");
    }
    if (!cache->ready()) {
        if (QFileInfo(directory).isSymLink()) { cache->directory.clear(); return fail("缓存路径不是普通目录。"); }
        if (QFileInfo::exists(directory)) QDir(directory).removeRecursively();
        if (!QDir().mkpath(directory)) return fail("无法创建地图转换目录。");
    } else {
        QFile marker(QDir(directory).filePath("ready.json"));
        if (marker.open(QIODevice::ReadWrite)) marker.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    }
    leases.insert(directory, cache);
    prune(root, budget);
    return cache;
}
bool ImportCache::ready() const {
    QFile marker(QDir(directory).filePath("ready.json"));
    if (!marker.open(QIODevice::ReadOnly)) return false;
    const auto files = QJsonDocument::fromJson(marker.readAll()).object();
    if (files.isEmpty()) return false;
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QFileInfo file(QDir(directory).filePath(it.key()));
        if (!file.isFile() || file.isSymLink() || file.size() != it.value().toInteger()) return false;
    }
    return true;
}
bool ImportCache::commit(const QStringList &files, QString *error) {
    if (sizeOf(directory) > budget) { *error = "转换结果超过地图缓存容量上限。"; return false; }
    QJsonObject manifest;
    for (const auto &name : files) {
        const QFileInfo file(QDir(directory).filePath(name));
        if (!file.isFile() || file.size() == 0) { *error = "地图转换结果不完整。"; return false; }
        manifest.insert(name, file.size());
    }
    QSaveFile marker(QDir(directory).filePath("ready.json"));
    auto bytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    if (!marker.open(QIODevice::WriteOnly) || marker.write(bytes) != bytes.size() || !marker.commit()) {
        *error = "无法完成地图缓存写入。"; return false;
    }
    prune(root, budget);
    return true;
}
ImportCache::~ImportCache() {
    if (directory.isEmpty() || !lock || !lock->isLocked()) return;
    if (!ready()) QDir(directory).removeRecursively();
    leases.remove(directory);
    lock->unlock();
    prune(root, budget);
}
void ImportCache::prune(const QString &root, qint64 budget) {
    struct Item { QFileInfo directory; qint64 bytes; QDateTime touched; };
    QList<Item> items;
    qint64 total = 0;
    const QRegularExpression managed("^[0-9a-f]{64}$");
    for (const auto &info : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        if (!managed.match(info.fileName()).hasMatch()) continue;
        const auto bytes = sizeOf(info.absoluteFilePath());
        total += bytes;
        items.append({info, bytes, QFileInfo(info.absoluteFilePath() + "/ready.json").lastModified()});
    }
    std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return a.touched < b.touched; });
    for (const auto &item : items) {
        if (total <= budget && item.touched.isValid()) continue;
        QLockFile guard(item.directory.absoluteFilePath() + ".lock");
        guard.setStaleLockTime(0);
        if (guard.tryLock() && QDir(item.directory.absoluteFilePath()).removeRecursively()) total -= item.bytes;
    }
}
