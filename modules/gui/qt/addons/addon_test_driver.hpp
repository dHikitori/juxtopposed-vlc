// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_ADDON_TEST_DRIVER_HPP
#define VLC_ADDON_TEST_DRIVER_HPP

#include <QObject>
#include <QPointer>
#include <QElapsedTimer>
#include <functional>
#include <memory>

class QLocalServer;
class QLocalSocket;
class QTimer;

namespace vlc::addons {
class AddonManager;
class AddonPackageSummaries;
class AddonDiscoveryService;

// The full app smoke test drives add-on actions through this owner. It is
// built only with the meson option addon-test-driver, which is off by
// default, and it starts only when VLC_ADDON_TEST_DRIVER_SOCKET names a
// socket path. It answers one line command at a time with one JSON line.
// It adds no timer while it waits for a command.
class AddonTestDriver : public QObject
{
public:
    static AddonTestDriver* create(AddonManager*, AddonPackageSummaries*, AddonDiscoveryService*, QObject* parent);
    ~AddonTestDriver() override;
    void close();
    // The metadata command answers with this report.
    void setMetadataReport(std::function<QVariantMap()>);
    // The panels command answers with this report.
    void setPanelsReport(std::function<QVariantMap()>);

private:
    AddonTestDriver(AddonManager*, AddonPackageSummaries*, AddonDiscoveryService*, QObject* parent);
    bool listen(const QString& path);
    void accept();
    void read();
    void run(const QString& command, const QString& argument);
    void reply(const QVariantMap&);
    // Calls done(true) when ready() is true after one of the owners changes,
    // or done(false) after the time limit. Only one wait exists at a time.
    void waitFor(std::function<bool()> ready, int timeoutMs, std::function<void(bool)> done);
    void endWait();

    void extensions();
    void install(const QString& directory);
    void discover(const QString& argument);

    QPointer<AddonManager> m_manager;
    QPointer<AddonPackageSummaries> m_summaries;
    QPointer<AddonDiscoveryService> m_discovery;
    std::function<QVariantMap()> m_metadata;
    std::function<QVariantMap()> m_panels;
    QLocalServer* m_server = nullptr;
    QPointer<QLocalSocket> m_client;
    QObject* m_wait = nullptr;
    std::shared_ptr<bool> m_waitAlive;
    QTimer* m_deadline = nullptr;
    bool m_busy = false;
    QElapsedTimer m_clock;
};
}

#endif
