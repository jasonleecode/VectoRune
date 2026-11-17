#include <QtQuickTest/quicktest.h>
#include <QtQml/QQmlEngine>
#include <QtCore/QCoreApplication>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtGui/QWheelEvent>
#include <QtQml/QQmlContext>
#include <QtGui/QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QDir>
#include <QFileInfo>

class InteractionSetup : public QObject {
    Q_OBJECT
    QTemporaryFile labelStyle;
public slots:
    QUrl localLabelStyle(const QUrl &url) {
        QFile file(url.toLocalFile());
        if (!file.open(QIODevice::ReadOnly) || !labelStyle.open())
            return {};
        auto style = QJsonDocument::fromJson(file.readAll()).object();
        const auto fonts = QFileInfo(file).dir().absoluteFilePath("../../plugins/garmin/resources/fonts");
        style.insert("glyphs", QUrl::fromLocalFile(fonts).toString() + "/{range}.pbf");
        labelStyle.write(QJsonDocument(style).toJson());
        labelStyle.flush();
        return QUrl::fromLocalFile(labelStyle.fileName());
    }
    QVariantMap labelPixels(QQuickItem *item, QPointF anchor) {
        const auto image = item->window()->grabWindow();
        const auto center = item->mapToScene(anchor) * image.devicePixelRatio();
        const int radius = qRound(110 * image.devicePixelRatio());
        int count = 0, markerCount = 0;
        QRect inkBounds, markerBounds;
        for (int y = qMax(0, int(center.y()) - radius); y < qMin(image.height(), int(center.y()) + radius); ++y)
            for (int x = qMax(0, int(center.x()) - radius); x < qMin(image.width(), int(center.x()) + radius); ++x) {
                const auto color = image.pixelColor(x, y);
                if (color.red() > 200 && color.green() < 80 && color.blue() < 80) {
                    ++markerCount;
                    markerBounds = markerBounds.united(QRect(x, y, 1, 1));
                }
                if (color.red() < 150 && color.green() < 150 && color.blue() < 150) {
                    ++count;
                    inkBounds = inkBounds.united(QRect(x, y, 1, 1));
                }
            }
        // Bounds measure position without an intensity centroid's sensitivity
        // to subpixel antialiasing. The marker uses the same map projection.
        const auto offset = (QRectF(inkBounds).center() - QRectF(markerBounds).center())
                            / image.devicePixelRatio();
        return {{"count", count}, {"markerCount", markerCount},
                {"x", offset.x()}, {"y", offset.y()}};
    }
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
