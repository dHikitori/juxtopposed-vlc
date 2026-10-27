// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_store_authority.hpp"
#include <QHash>
#include <QSet>
#include <QString>
#include <functional>
#include <mutex>
#include <optional>
#include <memory>

namespace vlc::addons {
// Trusted host code owns this store. It has no QObject, QML, or worker API.
class AddonSourceConsent final
{
    struct LiveEpoch;
    struct LiveOwner;
    struct LiveProfile;
public:
    enum class SourceKind { Git, Developer, Distribution };
    enum class IntentReason { PrimaryPlayback, QueueAdvance, Replay };
    struct Binding {
        SourceKind kind=SourceKind::Git;
        QString identity, contribution, accountReference;
        QString profile=QStringLiteral("public-https-media@1");
        bool operator==(const Binding&) const;
    };
    struct Ticket { QString id; Binding binding; };
    class LiveToken {
    public:
        bool current() const noexcept;
    private:
        friend class AddonSourceConsent;
        std::shared_ptr<LiveOwner> m_owner, m_receipt;
        std::shared_ptr<LiveProfile> m_profile;
        std::shared_ptr<LiveEpoch> m_entry;
        StoreAuthorityToken m_package;
        quint64 m_profileRevision=0, m_entryRevision=0;
    };
    class Authorization {
    public:
        const Binding& binding() const { return m_binding; }
        const QString& uri() const { return m_uri; }
        quint64 generation() const { return m_generation; }
        IntentReason reason() const { return m_reason; }
        LiveToken liveToken() const noexcept { return m_live; }
    private:
        friend class AddonSourceConsent;
        Binding m_binding;
        QString m_uri, m_approval, m_epoch, m_owner;
        quint64 m_generation=0, m_reset=0;
        IntentReason m_reason=IntentReason::PrimaryPlayback;
        LiveToken m_live;
    };
    struct Checks {
        // Both callbacks must be thread-safe. They must not call a QObject.
        // current checks source/account, enabled state, and the media grant.
        std::function<bool(const Binding&)> current;
        std::function<bool(const Binding&,const QString& admittedUri)> admitted;
    };
    struct Options {
        int approvals=256, tickets=64, intents=128;
        qint64 ticketLifetimeMs=120000, intentLifetimeMs=10000;
        // The clock runs under the store mutex. It must not re-enter the store.
        std::function<qint64()> now;
    };
    AddonSourceConsent(QString hostDirectory,Checks);
    AddonSourceConsent(QString hostDirectory,Checks,Options);
    ~AddonSourceConsent();
    static std::optional<Binding> canonical(const Binding&);
    std::optional<Ticket> capture(const Binding&,QString* error=nullptr);
    bool grant(const QString& ticket,QString* error=nullptr);
    bool revoke(const Binding&,QString* error=nullptr);
    bool approved(const Binding&) const;
    bool reset(QString* error=nullptr);
    // Only a native primary playback open can supply this generation/reason.
    // Enqueue, import, preparse, and background work must not call this method.
    QString mintIntent(const Binding&,const QString& admittedUri,quint64 generation,IntentReason);
    std::optional<Authorization> consumeIntent(const QString& token,const Binding&,const QString& admittedUri,
                                             quint64 generation,IntentReason);
    // Also check the captured package configuration before using a result.
    bool current(const Authorization&,const QString& admittedUri,quint64 generation) const;
    void close();
private:
    struct State;
    struct Dialog { Ticket ticket; QString epoch; quint64 revision=0; qint64 expires=0; quint64 localEpoch=0; };
    struct Intent { Authorization authorization; qint64 expires=0; };
    bool load(State*,QString* error=nullptr) const;
    bool save(const State&,QString*) const;
    bool directory(QString*) const;
    bool sourceCurrent(const Binding&) const;
    bool admissionCurrent(const Binding&,const QString&) const;
    qint64 prune();
    static QString key(const Binding&);
    static bool reasonValid(IntentReason);
    static std::shared_ptr<LiveProfile> liveProfile(const QString&);
    std::shared_ptr<LiveEpoch> liveEntry(const QString&) const;
    bool liveAllowed(const QString&) const;
    LiveToken liveToken(const QString&,const StoreAuthorityToken&) const;
    QString m_directory, m_file, m_owner;
    StoreAuthority m_packageAuthority;
    std::shared_ptr<LiveProfile> m_liveProfile;
    std::shared_ptr<LiveOwner> m_liveOwner;
    Checks m_checks;
    Options m_options;
    mutable std::mutex m_mutex;
    QHash<QString,Dialog> m_tickets;
    QHash<QString,Intent> m_intents;
    QSet<QString> m_withdrawn;
    QSet<quint64> m_issued;
    quint64 m_highest=0, m_floor=0, m_reset=0, m_ticketEpoch=0;
    qint64 m_lastNow=-1;
    bool m_closed=false, m_denyAll=false;
};
}
