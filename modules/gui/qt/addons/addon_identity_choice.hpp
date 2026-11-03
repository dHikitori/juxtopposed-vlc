// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_ADDON_IDENTITY_CHOICE_HPP
#define VLC_ADDON_IDENTITY_CHOICE_HPP

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <functional>
#include <memory>

struct input_item_t;
struct vlc_metadata_job;
class AddonMetadataService;

namespace vlc::addons {
class AddonAcceptedIdentities;

// The trusted identity choice dialog's owner. It asks the add-on identify
// providers which work the current episode belongs to, shows the offers, and
// waits for an explicit choice, also when there is only one offer. A choice
// is accepted through the metadata service, its metadata draft goes through
// the ordinary native Apply on the exact original item, and the accepted
// identity is saved with a new profile revision. QML sees display rows only;
// choice references stay in this owner.
class AddonIdentityChoice final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY changed FINAL)
    Q_PROPERTY(QString message READ message NOTIFY changed FINAL)
    Q_PROPERTY(QString query READ query NOTIFY changed FINAL)
public:
    // Return a held item, or null. The owner releases it.
    using CurrentItem = std::function<input_item_t*()>;
    AddonIdentityChoice(AddonMetadataService*, std::shared_ptr<AddonAcceptedIdentities>, CurrentItem,
                        QObject* parent = nullptr);
    ~AddonIdentityChoice() override;

    // idle, searching, choosing, applying, done or failed.
    QString state() const { return m_state; }
    QVariantList candidates() const { return m_rows; }
    QString message() const { return m_message; }
    QString query() const { return m_query; }

    // An empty query searches with the item's own series name.
    Q_INVOKABLE bool start(const QString& query = {});
    Q_INVOKABLE bool choose(int index);
    Q_INVOKABLE void cancel();
    void close();

signals:
    void changed();
    void accepted(quint64 revision);

private:
    void finished(const QString& consumer, const QString& status, const QVariantMap& data, const QString& error);
    void offer(const QVariantMap& data, const QString& error);
    void apply(const QString& status, const QVariantMap& data);
    void release();
    void fail(const QString& message);
    void set(const QString& state, const QString& message = {});

    QPointer<AddonMetadataService> m_service;
    std::shared_ptr<AddonAcceptedIdentities> m_accepted;
    CurrentItem m_current;
    std::shared_ptr<input_item_t> m_item;
    std::shared_ptr<struct vlc_metadata_job> m_anchor;
    QVariantMap m_local;
    QString m_target, m_consumer, m_choiceSet, m_selection, m_state = QStringLiteral("idle"), m_message, m_query;
    QStringList m_references;
    QVariantList m_rows;
    bool m_closed = false;
};

}

#endif
