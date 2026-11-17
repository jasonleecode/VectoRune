#include "mapconverter.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 8) return 1;
    MapConverter converter;
    QObject::connect(&converter, &MapConverter::changed, &app, [&] {
        if (converter.running()) return;
        const auto bytes = QJsonDocument(QJsonObject{{"status", converter.status()},
            {"result", converter.result().toString()}, {"canOpen", converter.canOpen()},
            {"log", converter.log()}}).toJson(QJsonDocument::Compact);
        puts(bytes.constData());
        app.exit(converter.result().isEmpty() ? 3 : 0);
    });
    QTimer::singleShot(0, &app, [&] {
        converter.start(QUrl::fromLocalFile(args[1]), QUrl::fromLocalFile(args[2]),
            args[4].split(',', Qt::SkipEmptyParts), args[5], args[3], args[6].toInt(), args[7].toInt());
        if (args.contains("--cancel")) QTimer::singleShot(0, &converter, &MapConverter::cancel);
    });
    QTimer::singleShot(120000, &app, [&] { converter.cancel(); app.exit(4); });
    return app.exec();
}
