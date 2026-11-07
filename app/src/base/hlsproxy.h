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

#ifndef HLSPROXY_H
#define HLSPROXY_H

#include <QList>
#include <QMap>
#include <QObject>
#include <QUrl>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QTcpSocket;

/*
 * Serves an HLS stream whose segments are plain MP3 (such as SoundCloud's hls_mp3_128_url) as a single
 * progressive MP3 over HTTP on 127.0.0.1, so that media players without HLS support can play it, and so that
 * it can be downloaded as one file.
 *
 * Playback URLs are served without a length (like an internet radio stream). Download URLs use chunked transfer
 * encoding, so that a failure partway through is reported as an error rather than as a complete, truncated file.
 * A "Range: bytes=N-" request header is honoured, so that downloads can be resumed.
 *
 * A "start=<ms>" query item starts the stream at that position, which is how a player seeks in a stream: it requests
 * a new URL (see seekUrl()) and adds the start position to the position reported by its media player.
 */
class HlsProxy : public QObject
{
    Q_OBJECT

public:
    ~HlsProxy();

    static HlsProxy* instance();

    // Returns the URL to play or download a stream returned by a QSoundCloud::StreamsRequest: a local proxy URL
    // if the stream's protocol is "hls", otherwise the stream's own URL.
    static QUrl streamUrl(const QVariantMap &stream, bool download = false);

    // Returns a local URL that serves the HLS media playlist at playlistUrl as one MP3 stream.
    QUrl proxyUrl(const QUrl &playlistUrl, bool download = false);

    // Returns the id of the stream served at url, or 0 if url is not a proxy URL.
    static int streamId(const QUrl &url);
    // Returns a URL that serves the same stream as url, starting at position (in milliseconds).
    static QUrl seekUrl(const QUrl &url, qint64 position);

    QNetworkAccessManager* networkAccessManager();

    bool hasStream(int id) const;
    QUrl playlistUrl(int id) const;
    QList<QUrl> segments(int id) const;
    // The duration of each segment in milliseconds. Empty if the playlist does not give them all.
    QList<qint64> segmentDurations(int id) const;
    void setSegments(int id, const QList<QUrl> &segments, const QList<qint64> &durations);

    // The duration of the stream in milliseconds, or 0 if it is not known yet. It is known once the stream's
    // playlist has been loaded, when durationChanged() is emitted.
    qint64 duration(int id) const;

Q_SIGNALS:
    void durationChanged(int id, qint64 duration);

private Q_SLOTS:
    void onNewConnection();

private:
    explicit HlsProxy(QObject *parent = 0);

    bool listen();

    struct Stream {
        QUrl playlistUrl;
        QList<QUrl> segments;
        QList<qint64> durations;
    };

    static HlsProxy *self;

    QTcpServer *m_server;
    QNetworkAccessManager *m_nam;

    QMap<int, Stream> m_streams;
    int m_nextId;
};

// Serves one HTTP request made to the proxy.
class HlsProxyConnection : public QObject
{
    Q_OBJECT

public:
    explicit HlsProxyConnection(QTcpSocket *socket, HlsProxy *proxy);

private Q_SLOTS:
    void onSocketReadyRead();
    void onSocketBytesWritten();
    void onSocketDisconnected();
    void onPlaylistFinished();
    void onSegmentFinished();

private:
    void handleRequest();
    void getPlaylist(const QUrl &url);
    void startStream();
    void getNextSegment();
    static int findMp3Frame(const QByteArray &data);
    void writeData(const QByteArray &data);
    void sendError(const QByteArray &status);
    void close();

    QTcpSocket *m_socket;
    HlsProxy *m_proxy;
    QNetworkReply *m_reply;

    QByteArray m_request;
    bool m_headOnly;
    bool m_chunked;
    bool m_headersSent;
    bool m_waitingForSocket;
    bool m_closed;

    int m_id;
    QList<QUrl> m_segments;
    QList<qint64> m_durations;
    int m_index;
    int m_retries;
    int m_playlistRedirects;
    qint64 m_skip;
    qint64 m_start;
    qint64 m_startOffset;
};

#endif // HLSPROXY_H
