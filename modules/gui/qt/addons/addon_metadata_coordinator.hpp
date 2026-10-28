// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <optional>

/** The host controls provider requests and validates results. This class
 * does not change live media. Backend callbacks run on this object's thread.
 * Connect the manager's operationFinished signal to the slot of that name.
 */
class AddonMetadataCoordinator : public QObject
{
    Q_OBJECT
public:
    struct NativeArtwork {
        QVariant descriptor;
        QString family;
        QVariantMap attribution;
    };
    struct Backend {
        std::function<QVariantList(const QString&)> providersForFeature;
        std::function<QString(const QString&, const QString&, const QVariantMap&)> invoke;
        std::function<void(const QString&)> cancel;
        // Only the native host can select this path. Its scope never enters
        // provider context or a portable request key.
        std::function<bool(const QString&)> requiresNativeScope;
        std::function<QString(const QString&, const QString&, const QVariantMap&, const QString&)> invokeNative;
        std::function<QVariantList(const QString&, const QString&, const QVariantList&, const QString&)> planProviders;
        // A native query only: no I/O, callback, target write, or Qt mutation.
        std::function<std::optional<NativeArtwork>(const QString&, const QVariantMap&, const QString&)> validateNativeArtwork;
    };

    explicit AddonMetadataCoordinator(Backend backend, QObject* parent = nullptr);
    ~AddonMetadataCoordinator() override;

    // Identification returns explicit choices. It does not change identity.
    Q_INVOKABLE QString identify(const QString& feature, const QVariantMap& context);
    Q_INVOKABLE QString searchAssets(const QString& feature, const QVariantMap& context,
                                     bool aggregate = false);
    Q_INVOKABLE QString enrich(const QString& feature, const QVariantMap& context,
                              const QStringList& requiredFields);
    QString enrichNative(const QString& feature, const QVariantMap& context,
                         const QStringList& requiredFields, const QString& privateScope);
    Q_INVOKABLE void cancel(const QString& consumer);
    Q_INVOKABLE void setProviderOrder(const QString& feature, const QStringList& providers);
    Q_INVOKABLE void invalidate();

public slots:
    void operationFinished(const QString& invocation, const QString& contribution,
                           const QString& status, const QVariantMap& data,
                           const QString& error);

signals:
    /** data carries contextGeneration/targetRevision and expectedFieldRevisions.
     * A consumer must compare them before it writes an enrichment patch.
     */
    void finished(const QString& consumer, const QString& status,
                  const QVariantMap& data, const QString& error);
    void canceled(const QString& consumer);

private:
    struct Consumer {
        QString feature;
        QString operation;
        QString privateScope;
        QVariantMap context;
        QVariantList providers;
        QStringList required;
        QVariantList candidates;
        QVariantMap fields;
        QVariantMap provenance;
        QVariantList labelArtwork;
        QVariantList ambiguousArtwork;
        QSet<QString> pending;
        QString status = QStringLiteral("no_match");
        QString error;
        int next = 0;
        int outstanding = 0;
        bool aggregate = false;
        bool ambiguous = false;
        bool entityAmbiguous = false;
    };
    struct Job {
        QString invocation;
        QString provider;
        QString operation;
        QString privateScope;
        QVariantMap context;
        QVariantMap registration;
        QSet<QString> waiters;
    };
    struct Cached {
        QString status;
        QVariantMap data;
        QString error;
        qint64 expires = 0;
    };

    QString start(const QString& feature, const QString& operation,
                  const QVariantMap& context, const QStringList& fields, bool aggregate,
                  const QString& privateScope = {});
    void advance(const QString& consumer);
    void subscribe(const QString& consumer, const QVariantMap& registration);
    void complete(const QString& invocation, const QString& contribution,
                  const QString& status, const QVariantMap& data, const QString& error);
    void consume(const QString& consumer, const QVariantMap& registration,
                 const QString& status, const QVariantMap& data, const QString& error);
    void finish(const QString& consumer);
    QStringList missing(const Consumer& consumer) const;
    void detach(const QString& consumer, const QSet<QString>& keys);
    QVariantMap providerContext(const Consumer& consumer) const;

    Backend m_backend;
    QHash<QString, Consumer> m_consumers;
    QHash<QString, Job> m_jobs;
    QHash<QString, QString> m_invocations;
    QHash<QString, Cached> m_cache;
    QHash<QString, Cached> m_backoff;
    QHash<QString, QStringList> m_order;
    QElapsedTimer m_clock;
    quint64 m_sequence = 0;
    quint64 m_policy = 0;
};
