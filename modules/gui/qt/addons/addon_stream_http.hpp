// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QMap>
#include <optional>

namespace vlc::addons {
class AddonStreamHttp final
{
public:
    struct Range { quint64 first = 0; std::optional<quint64> last; };
    struct Request { QByteArray method, path, ifRange, ifMatch; std::optional<Range> range; };
    struct Response {
        int status = 0;
        std::optional<quint64> length;
        std::optional<Range> range;
        std::optional<quint64> total;
        bool chunked = false;
        bool acceptsRanges = false;
        QByteArray type, location, etag;
    };
    static bool request(const QByteArray& header, const QByteArray& host, Request*);
    static bool response(const QByteArray& header, Response*);
    static bool range(const QByteArray&, Range*);
    static bool strongTag(const QByteArray&);
    static QByteArray rangeHeader(const Range&);
    class Chunks {
    public:
        bool feed(const QByteArray&, QByteArray* output, qsizetype limit = 32768);
        bool done() const { return m_done; }
        bool buffered() const { return !m_buffer.isEmpty(); }
    private:
        QByteArray m_buffer;
        quint64 m_left = 0;
        bool m_size = true, m_crlf = false, m_trailers = false, m_done = false, m_failed = false;
        qsizetype m_trailerBytes = 0;
    };
};
}
