// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "mapdocument.h"
#include "gpxtrack.h"

#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>

#include <QtGui/QGuiApplication>
#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QUrl>

#if QT_CONFIG(ssl)
#include <QtNetwork/QSslSocket>
#endif

#include <iostream>

using namespace Qt::StringLiterals;

static const char *help = R"(Usage:
--map <path>                                 -  Load a local MBTiles or Garmin IMG after startup
--mbtiles <path>                             -  Local vector MBTiles (default: data/maps/beijing.mbtiles)
--plugin.<parameter_name> <parameter_value>    -  Sets parameter = value for plugin
)";

static QVariantMap parseArgs(QStringList args)
{
    QVariantMap parameters;
    while (!args.isEmpty()) {
        QString param = args.takeFirst();
        if (param.startsWith(u"--plugin.")) {
            param.remove(0, 9);
            if (args.isEmpty() || args.constFirst().startsWith(u"--")) {
                parameters.insert(param, QVariant(true));
            } else {
                QString value = args.takeFirst();
                if (value == u"true" || value == u"on" || value == u"enabled") {
                    parameters.insert(param, QVariant(true));
                } else if (value == u"false" || value == u"off"
                           || value == u"disable") {
                    parameters.insert(param, QVariant(false));
                } else {
                    parameters.insert(param, QVariant(value));
                }
            }
        }
    }
    return parameters;
}

int main(int argc, char *argv[])
{
#if QT_CONFIG(library)
    const QString additionalLibraryPaths = qEnvironmentVariable("QTLOCATION_EXTRA_LIBRARY_PATH");
    for (const auto &p : additionalLibraryPaths.split(u':', Qt::SkipEmptyParts))
        QCoreApplication::addLibraryPath(p);
#endif

    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(u"QtLocation Mapviewer example"_s);

    QStringList args = QCoreApplication::arguments();
    args.removeFirst();
    if (args.contains(u"--help")) {
        std::cout << qPrintable(QCoreApplication::applicationName()) << "\n\n" << help;
        return 0;
    }

    QVariantMap parameters = parseArgs(args);
    QString mbtilesPath = QDir(QString::fromUtf8(MAPVIEWER_SOURCE_DIR))
                              .absoluteFilePath(u"../data/maps/beijing.mbtiles"_s);
    const auto mbtilesArg = args.indexOf(u"--mbtiles"_s);
    if (mbtilesArg >= 0) {
        if (mbtilesArg + 1 >= args.size() || args[mbtilesArg + 1].startsWith(u"--")) {
            std::cerr << "--mbtiles requires a file path\n";
            return 1;
        }
        mbtilesPath = args[mbtilesArg + 1];
    }
    MapDocument mapDocument;
    const auto initialMap = mapDocument.prepare(QUrl::fromLocalFile(QFileInfo(mbtilesPath).absoluteFilePath()));
    if (initialMap.contains(u"error"_s)) {
        std::cerr << qPrintable(initialMap.value(u"error"_s).toString()) << "\n";
        return 1;
    }
    if (!parameters.contains(u"maplibre.map.styles"_s))
        parameters.insert(u"maplibre.map.styles"_s,
                          initialMap.value(u"styleUrl"_s));
    if (!parameters.contains(u"maplibre.cache.directory"_s))
        parameters.insert(u"maplibre.cache.directory"_s,
                          QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                              + u"/maplibre"_s);
    // The plugin is bundled by qmaplibre_location_setup_plugins().
    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath() + u"/../PlugIns"_s);
    qInfo() << "MapLibre offline map:" << mbtilesPath;

    GpxTrack gpxTrack;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("gpxTrack", &gpxTrack);
    engine.rootContext()->setContextProperty("initialTrackFolder", QUrl::fromLocalFile(
        QDir(QString::fromUtf8(MAPVIEWER_SOURCE_DIR)).absoluteFilePath(u"../data/gpx"_s)));
    engine.rootContext()->setContextProperty("mapDocument", &mapDocument);
    engine.rootContext()->setContextProperty("initialMap", initialMap);
#if QT_CONFIG(ssl)
    engine.rootContext()->setContextProperty("supportsSsl", QSslSocket::supportsSsl());
#else
    engine.rootContext()->setContextProperty("supportsSsl", false);
#endif
    engine.loadFromModule("MapViewer", "Main");

    auto *item = engine.rootObjects().value(0);
    if (item == nullptr)
        return -1;

    QMetaObject::invokeMethod(item, "initializeProviders", QVariant::fromValue(parameters));
    const auto mapArg = args.indexOf(u"--map"_s);
    if (mapArg >= 0) {
        if (mapArg + 1 >= args.size() || args[mapArg + 1].startsWith(u"--")) {
            std::cerr << "--map requires a file path\n";
            return 1;
        }
        QMetaObject::invokeMethod(item, "loadMapFile", QVariant::fromValue(
            QUrl::fromLocalFile(QFileInfo(args[mapArg + 1]).absoluteFilePath())));
    }

    return application.exec();
}
