// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_registry_types.hpp"
#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QUrl>
#include <QUrlQuery>
#include <functional>
#include <memory>

namespace vlc::addons {

// Reads the hosted add-on registry. Use it on the GUI thread; replies are
// parsed, and the index cache read and written, on background work.
//
// The address is HTTPS. A loopback address (localhost, *.localhost,
// 127.0.0.1 or ::1) may use HTTP, for a development registry on this
// computer. Requests send no cookies or credentials and follow no redirect to
// another host. The index is cached on disk per address, with its ETag, so
// update badges work offline and a check costs one 304.
class AddonRegistryClient final : public QObject
{
    Q_OBJECT
public:
    static constexpr const char* DefaultAddress = "https://api.hakone.org/v1";
    static constexpr int TimeoutMs = 15000;

    struct Settings {
        bool enabled = true;
        QString address = QString::fromLatin1(DefaultAddress);
    };
    // codes: off, address, offline, timeout, http, too_large, not_found,
    // rate_limited, invalid, canceled.
    struct Failure {
        QString code, message;
    };
    template <typename T> using Done = std::function<void(const std::optional<T>&, const Failure&)>;

    explicit AddonRegistryClient(QString cacheDirectory, QObject* parent = nullptr);
    ~AddonRegistryClient() override;

    // Read before each request, so a changed setting applies at once.
    void setSettingsSource(std::function<Settings()>);
    Settings settings() const;
    // Empty when the address can be used, otherwise why it cannot.
    static QString addressProblem(const QString& address);
    static bool loopbackAddress(const QUrl&);

    // The last index read, from the network or the disk cache.
    const RegistryIndex& index() const { return m_index; }
    bool indexKnown() const { return m_indexKnown; }
    QString indexAddress() const { return m_indexAddress; }
    // Reads the cached index first, then asks the registry with its ETag.
    void loadIndex(std::function<void(bool changed, const Failure&)>);

    quint64 search(const QUrlQuery&, Done<RegistryPage>);
    quint64 addon(const QString& id, Done<RegistryAddon>);
    quint64 release(const QString& id, const QString& version, Done<RegistryRelease>);
    void cancel(quint64 request);

private:
    struct Reply { QByteArray body, etag; int status = 0; };
    using Raw = std::function<void(const std::optional<Reply>&, const Failure&)>;
    quint64 get(const QString& path, const QUrlQuery&, qsizetype maximumBytes, const QByteArray& etag, Raw);
    template <typename T>
    quint64 getParsed(const QString& path, const QUrlQuery&, std::function<bool(const QByteArray&, T*, bool, QString*)>, Done<T>);
    Failure failure(const QString& code) const;
    QString cachePath(const QString& address) const;

    QNetworkAccessManager m_network;
    std::function<Settings()> m_settings;
    QString m_cacheDirectory;
    QHash<quint64, QPointer<QNetworkReply>> m_replies;
    quint64 m_requests = 0;
    RegistryIndex m_index;
    QString m_indexAddress;
    QByteArray m_indexEtag;
    bool m_indexKnown = false, m_indexLoading = false;
    QList<std::function<void(bool, const Failure&)>> m_indexWaiting;
};
}
