#pragma once
#include <QString>
namespace RuntimePaths {
QString helper(const QString &name);
QString dataDirectory();
QString cacheDirectory();
}
