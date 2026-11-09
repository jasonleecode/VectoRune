#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantMap>

class GpxTrack : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QVariantMap read(const QUrl &url) const;
};
