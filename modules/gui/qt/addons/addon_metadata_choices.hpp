// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QHash>
#include <QElapsedTimer>
#include <QTimer>
#include <QVariantMap>
#include <functional>
#include <optional>

class AddonMetadataChoices : public QObject
{
public:
    // Call this API on the host thread. Selection does not change media.
    // Use only provider-tagged coordinator results as candidates. The host must
    // still validate the original target and revisions at the native commit.
    struct Backend {
        // Return a private native subject snapshot. Include the native URI,
        // policy/owner scope and revision tokens, not only public identity.
        // Use decimal strings for 64-bit native tokens. This snapshot stays
        // inside the host binding. It must not enter a provider or QML view.
        std::function<std::optional<QVariantMap>(const QString&)> mediaIdentity;
        std::function<bool(const QString&)> holdMedia;
        std::function<void(const QString&)> releaseMedia;
        std::function<QVariantMap(const QString&, const QString&)> registration;
    };
    struct Options {
        int maxSets = 64;
        int maxChoices = 8192;
        int maxSelections = 256;
        qint64 maxBytes = 16 * 1024 * 1024; // Serialized choice and selection payloads.
        int lifetimeMs = 300000;
    };
    explicit AddonMetadataChoices(Backend, QObject* parent = nullptr);
    AddonMetadataChoices(Backend, const Options&, QObject* parent = nullptr);
    ~AddonMetadataChoices() override;

    QVariantMap issue(const QString& feature, const QVariantMap& context, const QVariantList& candidates);
    QString select(const QString& choice, const QVariantMap& expectedContext);
    QVariantMap selected(const QString& selection, const QVariantMap& expectedContext) const;
    void releaseSet(const QString& set);
    void releaseSelection(const QString& selection);
    void invalidate();

private:
    struct Set { QString media, feature; QByteArray binding; QStringList choices; qint64 expires = 0; };
    struct Choice { QString set, provider, partition; QVariantMap identity, provenance; qint64 bytes = 0; };
    struct Selection { QString media, feature, provider, partition; QByteArray binding; QVariantMap identity, provenance; qint64 expires = 0, bytes = 0; };
    static QByteArray binding(const QVariantMap&, const QVariantMap& original);
    static bool providerCurrent(const QVariantMap&, const QString& provider, const QString& partition);
    static qint64 selectionBytes(const QString&, const Selection&);
    bool blocked() const;
    bool changed();
    bool expire();
    Backend m_backend;
    Options m_options;
    QHash<QString, Set> m_sets;
    QHash<QString, Choice> m_choices;
    QHash<QString, Selection> m_selections;
    qint64 m_bytes = 0;
    QElapsedTimer m_clock;
    QTimer m_sweep;
    quint64 m_epoch = 0;
    mutable bool m_active = false;
    bool m_invalidating = false, m_destroying = false, m_retired = false;
};
