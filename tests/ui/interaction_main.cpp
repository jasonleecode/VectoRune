#include <QtQuickTest/quicktest.h>
#include <QtQml/QQmlEngine>
#include <QtCore/QCoreApplication>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtGui/QWheelEvent>
#include <QtGui/QInputMethodEvent>
#include <QtQml/QQmlContext>
#include <QtGui/QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <numeric>

class InteractionSetup : public QObject {
    Q_OBJECT
    QTemporaryFile labelStyle;
    QElapsedTimer frameClock;
    QMutex frameMutex;
    QVector<double> frameTimes;
    qint64 previousFrame = 0;
    QMetaObject::Connection frameConnection;
public slots:
    QString benchmarkStyle() const { return qEnvironmentVariable("VECTORUNE_BENCH_STYLE"); }
    QVariantMap benchmarkOptions() const {
        return QJsonDocument::fromJson(qgetenv("VECTORUNE_BENCH_OPTIONS")).object().toVariantMap();
    }
    void startFrameTiming(QQuickItem *item) {
        disconnect(frameConnection);
        {
            QMutexLocker lock(&frameMutex);
            frameTimes.clear();
            previousFrame = 0;
            frameClock.start();
        }
        frameConnection = connect(item->window(), &QQuickWindow::frameSwapped, this, [this] {
            QMutexLocker lock(&frameMutex);
            const auto now = frameClock.nsecsElapsed();
            if (previousFrame)
                frameTimes.append((now - previousFrame) / 1000000.0);
            previousFrame = now;
        }, Qt::DirectConnection);
    }
    QVariantMap finishFrameTiming() {
        disconnect(frameConnection);
        QMutexLocker lock(&frameMutex);
        if (frameTimes.isEmpty()) return {};
        std::sort(frameTimes.begin(), frameTimes.end());
        const double mean = std::accumulate(frameTimes.begin(), frameTimes.end(), 0.0) / frameTimes.size();
        return {{"frames", frameTimes.size()}, {"mean_ms", mean}, {"fps", 1000.0 / mean},
                {"p50_ms", frameTimes[frameTimes.size() / 2]},
                {"p95_ms", frameTimes[qMin(frameTimes.size() - 1, frameTimes.size() * 95 / 100)]}};
    }
    QUrl localLabelStyle(const QUrl &url) {
        QFile file(url.toLocalFile());
        if (!file.open(QIODevice::ReadOnly) || !labelStyle.open())
            return {};
        labelStyle.resize(0);
        labelStyle.seek(0);
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
        const auto markerOffset = (QRectF(markerBounds).center() - center) / image.devicePixelRatio();
        return {{"count", count}, {"markerCount", markerCount},
                {"x", offset.x()}, {"y", offset.y()},
                {"markerX", markerOffset.x()}, {"markerY", markerOffset.y()}};
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
    void sendInputMethod(QQuickItem *item, const QString &preedit, const QString &commit) {
        QInputMethodEvent event(preedit, {});
        event.setCommitString(commit);
        QCoreApplication::sendEvent(item, &event);
    }
    QStringList backgroundSamples(QQuickItem *item) {
        const auto image = item->window()->grabWindow();
        QStringList colors;
        for (const auto &point : {QPointF(400, 350), QPointF(750, 550)}) {
            const auto pixel = item->mapToScene(point) * image.devicePixelRatio();
            colors.append(image.pixelColor(pixel.toPoint()).name());
        }
        return colors;
    }
    QStringList mapEdgeSamples(QQuickItem *item) {
        const auto image = item->window()->grabWindow();
        QStringList colors;
        for (const auto &point : {QPointF(16, 16), QPointF(item->width() - 16, 16),
                                  QPointF(16, item->height() - 16),
                                  QPointF(item->width() - 16, item->height() - 16)}) {
            const auto pixel = (item->mapToScene(point) * image.devicePixelRatio()).toPoint();
            colors.append(image.rect().contains(pixel) ? image.pixelColor(pixel).name() : "outside");
        }
        return colors;
    }
};

QUICK_TEST_MAIN_WITH_SETUP(mapviewer_interactions, InteractionSetup)
#include "interaction_main.moc"
