#include <QtQuickTest/quicktest.h>
#include <QtQml/QQmlEngine>
#include <QtCore/QCoreApplication>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtGui/QWheelEvent>
#include <QtQml/QQmlContext>

class InteractionSetup : public QObject {
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine *engine) {
        QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath() + "/../PlugIns");
        engine->rootContext()->setContextProperty("input", this);
    }
    void sendPixelWheel(QQuickItem *item, QPointF point, int delta) {
        const auto local = item->mapToScene(point);
        const auto global = item->window()->mapToGlobal(local);
        QWheelEvent event(local, global, QPoint(0, delta), QPoint(), Qt::NoButton,
                          Qt::NoModifier, Qt::ScrollUpdate, false);
        QCoreApplication::sendEvent(item->window(), &event);
    }
};

QUICK_TEST_MAIN_WITH_SETUP(mapviewer_interactions, InteractionSetup)
#include "interaction_main.moc"
