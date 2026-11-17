#pragma once

#include <QObject>

#if !QT_CONFIG(permissions)
// Qt builds without permission support omit QtCore's QML LocationPermission.
// There is no application permission prompt on these platforms; the positioning
// backend still reports unavailable services or failed position requests.
class LocationPermissionFallback : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Qt::PermissionStatus status READ status NOTIFY statusChanged)
    Q_PROPERTY(Accuracy accuracy MEMBER m_accuracy)
    Q_PROPERTY(Availability availability MEMBER m_availability)

public:
    enum Accuracy { Approximate, Precise };
    Q_ENUM(Accuracy)
    enum Availability { WhenInUse, Always };
    Q_ENUM(Availability)
    explicit LocationPermissionFallback(QObject *parent = nullptr) : QObject(parent) {}
    Qt::PermissionStatus status() const { return Qt::PermissionStatus::Granted; }
    Q_INVOKABLE void request() { emit statusChanged(); }

signals:
    void statusChanged();

private:
    Accuracy m_accuracy = Approximate;
    Availability m_availability = WhenInUse;
};
#endif
