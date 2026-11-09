#include "geometryindex.h"
#include <QCache>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
#include <QUuid>
#include <stdexcept>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        if (argc != 2 && argc != 6)
            return 1;
        GeometryIndex index(QString::fromLocal8Bit(argv[1]), false);
        // A deterministic offline CLI is also useful for fixtures and prewarming.
        if (argc == 6) {
            bool ok[3];
            int z = QString(argv[2]).toInt(&ok[0]), x = QString(argv[3]).toInt(&ok[1]),
                y = QString(argv[4]).toInt(&ok[2]);
            if (!ok[0] || !ok[1] || !ok[2])
                return 2;
            int read = 0;
            auto bytes = index.tile(z, x, y, &read);
            QFile out(QString::fromLocal8Bit(argv[5]));
            if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size())
                return 2;
            QTextStream(stdout) << QJsonDocument(QJsonObject{{"featuresRead", read}, {"bytes", bytes.size()}})
                                       .toJson(QJsonDocument::Compact)
                                << '\n';
            return 0;
        }
        QSet<QTcpSocket *> clients;
        QTcpServer server;
        server.setMaxPendingConnections(8);
        if (!server.listen(QHostAddress::LocalHost, 0))
            throw std::runtime_error(server.errorString().toStdString());
        const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QCache<QString, QByteArray> cache(16 * 1024 * 1024);
        QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
            while (auto *socket = server.nextPendingConnection()) {
                if (clients.size() >= 8) {
                    socket->abort();
                    socket->deleteLater();
                    continue;
                }
                clients.insert(socket);
                socket->setReadBufferSize(8193);
                QObject::connect(socket, &QTcpSocket::disconnected, &app, [&, socket] {
                    clients.remove(socket);
                    socket->deleteLater();
                });
                QTimer::singleShot(15000, socket, [socket] {
                    socket->abort();
                    socket->deleteLater();
                });
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                    if (socket->property("answered").toBool())
                        return;
                    auto request = socket->property("request").toByteArray() + socket->readAll();
                    auto reply = [socket](int status, const QByteArray &body) {
                        socket->setProperty("answered", true);
                        socket->write("HTTP/1.1 " + QByteArray::number(status) +
                                      (status == 200 ? " OK\r\n" : " Error\r\n") +
                                      "Content-Type: application/vnd.mapbox-vector-tile\r\nConnection: "
                                      "close\r\nContent-Length: " +
                                      QByteArray::number(body.size()) + "\r\n\r\n");
                        socket->write(body);
                        socket->disconnectFromHost();
                    };
                    if (request.size() > 8192) {
                        reply(400, {});
                        return;
                    }
                    socket->setProperty("request", request);
                    if (!request.contains("\r\n\r\n"))
                        return;
                    const auto parts = request.left(request.indexOf("\r\n")).split(' ');
                    if (parts.size() != 3 || parts[0] != "GET") {
                        reply(400, {});
                        return;
                    }
                    const QRegularExpression pattern("^/" + token +
                                                     "/(\\d{1,2})/(\\d{1,6})/(\\d{1,6})\\.pbf$");
                    const auto match = pattern.match(QString::fromLatin1(parts[1]));
                    if (!match.hasMatch()) {
                        reply(404, {});
                        return;
                    }
                    QString key = QString::fromLatin1(parts[1]);
                    if (auto *bytes = cache.object(key)) {
                        reply(200, *bytes);
                        return;
                    }
                    try {
                        auto bytes = index.tile(match.captured(1).toInt(), match.captured(2).toInt(),
                                                match.captured(3).toInt());
                        reply(200, bytes);
                        int size = bytes.size();
                        cache.insert(key, new QByteArray(std::move(bytes)), size);
                    } catch (const std::exception &e) {
                        qWarning("Tile error: %s", e.what());
                        reply(500, {});
                    }
                });
            }
        });
        QTextStream out(stdout);
        out << QJsonDocument(
                   QJsonObject{
                       {"url", QString("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(token)}})
                   .toJson(QJsonDocument::Compact)
            << '\n';
        out.flush();
        return app.exec();
    } catch (const std::exception &e) {
        QTextStream(stderr) << e.what() << '\n';
        return 2;
    }
}
