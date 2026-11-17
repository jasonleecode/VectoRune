#pragma once
#include <QJsonObject>
#include <QString>

// Apply a display preset without changing the vector tile data.
QJsonObject applyStylePalette(QJsonObject style, const QString &preset);
