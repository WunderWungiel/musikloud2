/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "hlsproxy.h"
#include "logger.h"
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

// The number of streams remembered, so that a player reconnecting to a stream does not get a dead URL.
static const int MAX_STREAMS = 20;
// The maximum size of an HTTP request header.
static const int MAX_REQUEST_SIZE = 8192;
// The next segment is not requested while more than this is waiting to be written to the client, so that a whole
// track is not buffered in memory when the client reads at playback speed.
static const qint64 MAX_BUFFERED_BYTES = 256 * 1024;
static const int MAX_SEGMENT_RETRIES = 3;
static const int MAX_PLAYLIST_REDIRECTS = 3;

HlsProxy* HlsProxy::self = 0;

HlsProxy::HlsProxy(QObject *parent) :
    QObject(parent),
    m_server(new QTcpServer(this)),
    m_nam(new QNetworkAccessManager(this)),
    m_nextId(1)
{
    // The server only accepts local connections, so it must never go through the application proxy.
    m_server->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    connect(m_server, SIGNAL(newConnection()), this, SLOT(onNewConnection()));
}

HlsProxy::~HlsProxy() {
    if (self == this) {
        self = 0;
    }
}

HlsProxy* HlsProxy::instance() {
    return self ? self : self = new HlsProxy;
}

QUrl HlsProxy::streamUrl(const QVariantMap &stream, bool download) {
    const QUrl url = stream.value("url").toString();

    if (stream.value("protocol") == "hls") {
        return instance()->proxyUrl(url, download);
    }

    return url;
}

bool HlsProxy::listen() {
    if (m_server->isListening()) {
        return true;
    }

    if (!m_server->listen(QHostAddress::LocalHost)) {
        Logger::log("HlsProxy::listen(). Error: " + m_server->errorString());
        return false;
    }

    Logger::log("HlsProxy::listen(). Listening on port " + QString::number(m_server->serverPort()),
                Logger::MediumVerbosity);
    return true;
}

QUrl HlsProxy::proxyUrl(const QUrl &playlistUrl, bool download) {
    if (!listen()) {
        return playlistUrl;
    }

    const int id = m_nextId++;
    Stream stream;
    stream.playlistUrl = playlistUrl;
    m_streams.insert(id, stream);

    while (m_streams.size() > MAX_STREAMS) {
        m_streams.erase(m_streams.begin());
    }

    // The .mp3 suffix lets media frameworks that choose a decoder by file extension (such as Symbian MMF) pick MP3.
    QUrl url(QString("http://127.0.0.1:%1/%2.mp3").arg(m_server->serverPort()).arg(id));

    if (download) {
        url.addQueryItem("download", "1");
    }

    Logger::log(QString("HlsProxy::proxyUrl(). Stream %1: %2").arg(id).arg(playlistUrl.toString()),
                Logger::HighVerbosity);
    return url;
}

QNetworkAccessManager* HlsProxy::networkAccessManager() {
    return m_nam;
}

bool HlsProxy::hasStream(int id) const {
    return m_streams.contains(id);
}

QUrl HlsProxy::playlistUrl(int id) const {
    return m_streams.value(id).playlistUrl;
}

QList<QUrl> HlsProxy::segments(int id) const {
    return m_streams.value(id).segments;
}

void HlsProxy::setSegments(int id, const QList<QUrl> &segments) {
    if (m_streams.contains(id)) {
        m_streams[id].segments = segments;
    }
}

void HlsProxy::onNewConnection() {
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        new HlsProxyConnection(socket, this);
    }
}

HlsProxyConnection::HlsProxyConnection(QTcpSocket *socket, HlsProxy *proxy) :
    QObject(proxy),
    m_socket(socket),
    m_proxy(proxy),
    m_reply(0),
    m_headOnly(false),
    m_chunked(false),
    m_headersSent(false),
    m_waitingForSocket(false),
    m_closed(false),
    m_id(0),
    m_index(0),
    m_retries(0),
    m_playlistRedirects(0),
    m_skip(0)
{
    socket->setParent(this);
    connect(socket, SIGNAL(readyRead()), this, SLOT(onSocketReadyRead()));
    connect(socket, SIGNAL(bytesWritten(qint64)), this, SLOT(onSocketBytesWritten()));
    connect(socket, SIGNAL(disconnected()), this, SLOT(onSocketDisconnected()));
}

void HlsProxyConnection::onSocketReadyRead() {
    if (m_closed) {
        return;
    }

    if (m_id > 0) {
        // The request has already been handled. Ignore anything else the client sends.
        m_socket->readAll();
        return;
    }

    m_request += m_socket->readAll();

    if (m_request.contains("\r\n\r\n")) {
        handleRequest();
    }
    else if (m_request.size() > MAX_REQUEST_SIZE) {
        sendError("400 Bad Request");
    }
}

void HlsProxyConnection::handleRequest() {
    const QList<QByteArray> lines = m_request.left(m_request.indexOf("\r\n\r\n")).split('\n');
    const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');

    if (requestLine.size() < 2) {
        sendError("400 Bad Request");
        return;
    }

    const QByteArray method = requestLine.at(0);

    if ((method != "GET") && (method != "HEAD")) {
        sendError("405 Method Not Allowed");
        return;
    }

    m_headOnly = (method == "HEAD");

    const QUrl path = QUrl::fromEncoded("http://localhost" + requestLine.at(1));
    QString name = path.path().mid(1);
    name.chop(4); // ".mp3"
    m_id = name.toInt();
    m_chunked = (path.queryItemValue("download") == "1");

    for (int i = 1; i < lines.size(); i++) {
        const QByteArray line = lines.at(i).trimmed();

        if (line.toLower().startsWith("range:")) {
            // Only the "bytes=N-" form, as sent when resuming a download, is supported.
            QByteArray range = line.mid(6).trimmed();

            if (range.startsWith("bytes=")) {
                range = range.mid(6);
                m_skip = qMax(qint64(0), range.left(range.indexOf('-')).toLongLong());
            }
        }
    }

    Logger::log(QString("HlsProxyConnection::handleRequest(). %1 stream %2, offset %3")
                .arg(QString::fromUtf8(method)).arg(m_id).arg(m_skip), Logger::MediumVerbosity);

    if ((m_id <= 0) || (!m_proxy->hasStream(m_id))) {
        m_id = -1;
        sendError("404 Not Found");
        return;
    }

    m_segments = m_proxy->segments(m_id);

    if (m_segments.isEmpty()) {
        getPlaylist(m_proxy->playlistUrl(m_id));
    }
    else {
        startStream();
    }
}

void HlsProxyConnection::getPlaylist(const QUrl &url) {
    m_reply = m_proxy->networkAccessManager()->get(QNetworkRequest(url));
    connect(m_reply, SIGNAL(finished()), this, SLOT(onPlaylistFinished()));
}

void HlsProxyConnection::onPlaylistFinished() {
    QNetworkReply *reply = m_reply;
    m_reply = 0;
    reply->deleteLater();

    if (m_closed) {
        return;
    }

    const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();

    if ((!redirect.isEmpty()) && (m_playlistRedirects < MAX_PLAYLIST_REDIRECTS)) {
        m_playlistRedirects++;
        getPlaylist(reply->url().resolved(redirect));
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        Logger::log("HlsProxyConnection::onPlaylistFinished(). Error: " + reply->errorString());
        sendError("502 Bad Gateway");
        return;
    }

    const QStringList lines = QString::fromUtf8(reply->readAll()).split('\n');
    QList<QUrl> segments;
    QUrl variant;

    for (int i = 0; i < lines.size(); i++) {
        const QString line = lines.at(i).trimmed();

        if (line.isEmpty()) {
            continue;
        }

        if (line.startsWith("#")) {
            if ((line.startsWith("#EXT-X-KEY")) && (!line.contains("METHOD=NONE"))) {
                Logger::log("HlsProxyConnection::onPlaylistFinished(). Error: Encrypted streams are not supported");
                sendError("502 Bad Gateway");
                return;
            }

            if ((line.startsWith("#EXT-X-STREAM-INF")) && (variant.isEmpty())) {
                // A master playlist. Use its first variant, which is on the next URI line.
                for (int j = i + 1; j < lines.size(); j++) {
                    const QString uri = lines.at(j).trimmed();

                    if ((!uri.isEmpty()) && (!uri.startsWith("#"))) {
                        variant = reply->url().resolved(QUrl::fromEncoded(uri.toUtf8()));
                        break;
                    }
                }
            }

            continue;
        }

        segments << reply->url().resolved(QUrl::fromEncoded(line.toUtf8()));
    }

    if ((!variant.isEmpty()) && (m_playlistRedirects < MAX_PLAYLIST_REDIRECTS)) {
        m_playlistRedirects++;
        getPlaylist(variant);
        return;
    }

    if (segments.isEmpty()) {
        Logger::log("HlsProxyConnection::onPlaylistFinished(). Error: No segments found");
        sendError("502 Bad Gateway");
        return;
    }

    Logger::log(QString("HlsProxyConnection::onPlaylistFinished(). Stream %1 has %2 segments").arg(m_id)
                .arg(segments.size()), Logger::MediumVerbosity);
    m_segments = segments;
    m_proxy->setSegments(m_id, segments);
    startStream();
}

void HlsProxyConnection::startStream() {
    QByteArray headers;

    if (m_chunked) {
        headers = (m_skip > 0) ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
        headers += "Transfer-Encoding: chunked\r\n";
    }
    else {
        // No length is sent, so the end of the stream is marked by closing the connection.
        headers = (m_skip > 0) ? "HTTP/1.0 206 Partial Content\r\n" : "HTTP/1.0 200 OK\r\n";
    }

    headers += "Content-Type: audio/mpeg\r\n"
               "Cache-Control: no-cache\r\n"
               "Connection: close\r\n\r\n";
    m_socket->write(headers);
    m_headersSent = true;

    if (m_headOnly) {
        close();
        return;
    }

    m_index = 0;
    getNextSegment();
}

void HlsProxyConnection::getNextSegment() {
    if (m_closed) {
        return;
    }

    if (m_index >= m_segments.size()) {
        Logger::log(QString("HlsProxyConnection::getNextSegment(). Stream %1 completed").arg(m_id),
                    Logger::MediumVerbosity);

        if (m_chunked) {
            m_socket->write("0\r\n\r\n");
        }

        close();
        return;
    }

    if (m_socket->bytesToWrite() > MAX_BUFFERED_BYTES) {
        m_waitingForSocket = true;
        return;
    }

    m_waitingForSocket = false;
    m_reply = m_proxy->networkAccessManager()->get(QNetworkRequest(m_segments.at(m_index)));
    connect(m_reply, SIGNAL(finished()), this, SLOT(onSegmentFinished()));
}

void HlsProxyConnection::onSegmentFinished() {
    QNetworkReply *reply = m_reply;
    m_reply = 0;
    reply->deleteLater();

    if (m_closed) {
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        Logger::log(QString("HlsProxyConnection::onSegmentFinished(). Stream %1, segment %2. Error: %3")
                    .arg(m_id).arg(m_index).arg(reply->errorString()));

        if (m_retries < MAX_SEGMENT_RETRIES) {
            m_retries++;
            getNextSegment();
        }
        else {
            // Abort rather than close, so that the client does not take a truncated stream as complete.
            m_closed = true;
            m_socket->abort();
            deleteLater();
        }

        return;
    }

    m_retries = 0;
    QByteArray data = reply->readAll();

    if (m_skip > 0) {
        const qint64 skip = qMin(m_skip, qint64(data.size()));
        data = data.mid(skip);
        m_skip -= skip;
    }

    writeData(data);
    m_index++;
    getNextSegment();
}

void HlsProxyConnection::writeData(const QByteArray &data) {
    if (data.isEmpty()) {
        return;
    }

    if (m_chunked) {
        m_socket->write(QByteArray::number(data.size(), 16) + "\r\n");
        m_socket->write(data);
        m_socket->write("\r\n");
    }
    else {
        m_socket->write(data);
    }
}

void HlsProxyConnection::onSocketBytesWritten() {
    if ((m_waitingForSocket) && (m_socket->bytesToWrite() <= MAX_BUFFERED_BYTES)) {
        getNextSegment();
    }
}

void HlsProxyConnection::sendError(const QByteArray &status) {
    if (!m_headersSent) {
        m_socket->write("HTTP/1.0 " + status + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        m_headersSent = true;
        close();
    }
    else {
        m_closed = true;
        m_socket->abort();
        deleteLater();
    }
}

void HlsProxyConnection::close() {
    m_closed = true;
    // Closes once everything buffered has been written. disconnected() then deletes this connection.
    m_socket->disconnectFromHost();

    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        deleteLater();
    }
}

void HlsProxyConnection::onSocketDisconnected() {
    if (!m_closed) {
        Logger::log(QString("HlsProxyConnection::onSocketDisconnected(). Stream %1, client disconnected").arg(m_id),
                    Logger::MediumVerbosity);
    }

    m_closed = true;

    if (m_reply) {
        m_reply->abort();
    }

    deleteLater();
}
