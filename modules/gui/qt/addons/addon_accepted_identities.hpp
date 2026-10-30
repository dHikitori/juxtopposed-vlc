// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_ADDON_ACCEPTED_IDENTITIES_HPP
#define VLC_ADDON_ACCEPTED_IDENTITIES_HPP

#include <QString>
#include <QVariantMap>
#include <optional>

namespace vlc::addons {

// Identities the user accepted in the identity choice dialog, keyed by the
// local identity they correct (for an episode: its series). Each accept takes
// the next revision in the profile, so later requests and cache entries can
// bind to it. The file is shared by every window of the profile: writes take
// a lock file and replace the file atomically. Each read loads the small
// file again, so it sees other windows' writes without a timer or a watch.
class AddonAcceptedIdentities final
{
public:
    struct Record {
        QVariantMap identity;
        QString provider;
        quint64 revision = 0;
    };
    static constexpr int MaximumRecords = 4096;

    explicit AddonAcceptedIdentities(QString profileDirectory);
    // The key a local identity map (MediaIdentity::toMap) is accepted under,
    // or empty when that kind has no accepted identity.
    static QString key(const QVariantMap& localIdentity);
    std::optional<Record> find(const QString& key) const;
    // Return the new revision, or 0 on failure.
    quint64 accept(const QString& key, const QVariantMap& identity, const QString& provider,
                   QString* error = nullptr);
    bool forget(const QString& key, QString* error = nullptr);
    QString path() const { return m_path; }

private:
    struct Snapshot {
        QVariantMap records;
        quint64 revision = 0;
    };
    bool load(Snapshot&, QString* error) const;
    bool store(const Snapshot&, QString* error) const;
    QString m_path;
};

}

#endif
