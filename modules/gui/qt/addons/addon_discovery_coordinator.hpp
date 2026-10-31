// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QHash>
#include <QVariantMap>
#include <functional>

/** Host-owned discovery references and lazy playback authorization.
 * Discovery results are data; this class never changes a playlist or player.
 */
class AddonDiscoveryCoordinator : public QObject
{
    Q_OBJECT
public:
    using PlayableCompletion = std::function<void(const QString& playbackHandle, const QString& error)>;
    struct Backend {
        std::function<QVariantList()> sources;
        std::function<QString(const QString&, const QString&, const QVariantMap&)> invoke;
        std::function<void(const QString&)> cancel;
        // This trusted adapter must enforce all nested stream destinations and
        // current gesture/session authority. An absent adapter denies playback.
        std::function<QString(const QString&, const QVariantMap&, PlayableCompletion)> authorizePlayable;
        std::function<void(const QString&)> cancelPlayable;
        // Memory-only display freshness. It must not emit signals, call
        // external code, or reenter/destroy this owner. Execution keeps its
        // own authority; this callback never replaces that authority.
        // Isolated adapters may omit this when they own invalidation.
        std::function<bool()> current;
    };
    explicit AddonDiscoveryCoordinator(Backend backend, QObject* parent = nullptr);
    ~AddonDiscoveryCoordinator() override;

    Q_INVOKABLE QVariantList sources() const;
    Q_INVOKABLE bool supports(const QString& source, const QString& operation) const;
    QVariantMap sourceIdentity(const QString& source) const;
    bool isCurrent() const;
    Q_INVOKABLE QString roots(const QString& source, const QString& view, quint64 generation, const QString& cursor = {});
    Q_INVOKABLE QString browse(const QString& node, const QString& view, quint64 generation,
                              const QString& cursor = {});
    Q_INVOKABLE QString search(const QString& source, const QString& query, const QVariantMap& filters,
                              const QString& view, quint64 generation, const QString& cursor = {});
    Q_INVOKABLE QString suggest(const QString& source, const QVariantMap& context, int limit,
                               const QString& view, quint64 generation);
    // A bounded change feed for a view the user has open: node is a folder
    // handle of this view, or empty for the source root. revision is the
    // source's last revision. The result has items and updates as a page,
    // removed (handles), revision and nextCheckSeconds (60 to 3600).
    Q_INVOKABLE QString subscribe(const QString& source, const QString& node, const QString& revision,
                                  const QString& view, quint64 generation);
    Q_INVOKABLE QVariantMap queueReference(const QString& item) const;
    Q_INVOKABLE QString resolve(const QString& item, const QString& intent,
                               const QString& view, quint64 generation);
    Q_INVOKABLE QString resolveQueued(const QVariantMap& reference, const QString& intent,
                                     const QString& view, quint64 generation);
    Q_INVOKABLE void cancel(const QString& request);
    Q_INVOKABLE void cancelView(const QString& view);

public slots:
    void operationFinished(const QString& invocation, const QString& contribution,
                           const QString& status, const QVariantMap& data, const QString& error);
signals:
    void finished(const QString& request, const QString& status, const QVariantMap& data, const QString& error);
    void canceled(const QString& request);

private:
    struct Handle {
        QString source, item, kind, partition, account, view, stable;
        quint64 generation = 0;
        QVariantMap display;
    };
    struct Cursor {
        QString source, operation, partition, view, fingerprint;
        quint64 generation = 0;
        QVariant value;
    };
    struct Request {
        QString source, operation, view, invocation, gateJob, intent;
        quint64 generation = 0;
        QVariantMap context, registration;
    };
    QVariantMap registration(const QString& source) const;
    bool available(const QVariantMap& registration, const QString& operation) const;
    bool current(const Handle& handle) const;
    bool enterView(const QString& view, quint64 generation);
    QString rejected(const QString& view, quint64 generation, const QString& status, const QString& error);
    QString start(const QString& source, const QString& operation, QVariantMap context,
                  const QString& view, quint64 generation, const QString& cursor = {}, const QString& intent = {});
    void complete(const QString& invocation, const QString& source, const QString& status,
                  const QVariantMap& data, const QString& error);
    void page(const QString& request, const QVariantMap& data);
    void authorize(const QString& request, const QVariantMap& proposal);
    void finish(const QString& request, const QString& status, QVariantMap data = {}, const QString& error = {});
    QString resolveStable(const QString& source, const QString& item, const QString& account,
                          const QString& intent, const QString& view, quint64 generation);
    void revokeHandles(const QString& view);

    Backend m_backend;
    QHash<QString, quint64> m_views;
    QHash<QString, quint64> m_closedViews;
    QHash<QString, Request> m_requests;
    QHash<QString, QString> m_invocations;
    QHash<QString, Handle> m_handles;
    QHash<QString, QString> m_stable;
    QHash<QString, Cursor> m_cursors;
};
