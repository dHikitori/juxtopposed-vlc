// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "addon_test_driver.hpp"
#include "addon_manager.hpp"
#include "addon_package_summaries.hpp"
#include "addon_discovery_service.hpp"
#include "addon_process_launch.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

#include <memory>

namespace vlc::addons {

AddonTestDriver* AddonTestDriver::create(AddonManager* manager, AddonPackageSummaries* summaries,
                                         AddonDiscoveryService* discovery, QObject* parent)
{
    const auto path = qEnvironmentVariable("VLC_ADDON_TEST_DRIVER_SOCKET");
    if (path.isEmpty())
        return nullptr;
    auto* driver = new AddonTestDriver(manager, summaries, discovery, parent);
    if (!driver->listen(path)) {
        delete driver;
        return nullptr;
    }
    return driver;
}

AddonTestDriver::AddonTestDriver(AddonManager* manager, AddonPackageSummaries* summaries,
                                 AddonDiscoveryService* discovery, QObject* parent)
    : QObject(parent), m_manager(manager), m_summaries(summaries), m_discovery(discovery)
{
}

AddonTestDriver::~AddonTestDriver()
{
    close();
}

bool AddonTestDriver::listen(const QString& path)
{
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(path))
        return false;
    connect(m_server, &QLocalServer::newConnection, this, [this] { accept(); });
    return true;
}

void AddonTestDriver::close()
{
    endWait();
    if (m_client)
        m_client->abort();
    if (m_server)
        m_server->close();
}

void AddonTestDriver::setMetadataReport(std::function<QVariantMap()> report)
{
    m_metadata = std::move(report);
}

void AddonTestDriver::accept()
{
    while (auto* socket = m_server->nextPendingConnection()) {
        // One client at a time. A new client replaces the old one.
        if (m_client)
            m_client->abort();
        m_client = socket;
        connect(socket, &QLocalSocket::readyRead, this, [this] { read(); });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void AddonTestDriver::read()
{
    while (m_client && !m_busy && m_client->canReadLine()) {
        const auto line = QString::fromUtf8(m_client->readLine()).trimmed();
        const auto space = line.indexOf(QLatin1Char(' '));
        run(space < 0 ? line : line.left(space), space < 0 ? QString() : line.mid(space + 1));
    }
}

void AddonTestDriver::reply(const QVariantMap& result)
{
    m_busy = false;
    if (m_client)
        m_client->write(QJsonDocument(QJsonObject::fromVariantMap(result)).toJson(QJsonDocument::Compact) + '\n');
    // A command can arrive while the previous one runs.
    QTimer::singleShot(0, this, [this] { read(); });
}

void AddonTestDriver::run(const QString& command, const QString& argument)
{
    m_busy = true;
    m_clock.start();
    if (command == QLatin1String("ping"))
        reply({{"ok", true}});
    else if (command == QLatin1String("stats"))
        reply({{"ok", true}, {"launches", qint64(helperLaunchCount())}});
    else if (command == QLatin1String("extensions"))
        extensions();
    else if (command == QLatin1String("install"))
        install(argument);
    else if (command == QLatin1String("discover"))
        discover(argument);
    else if (command == QLatin1String("metadata") && m_metadata) {
        auto result = m_metadata();
        result.insert(QStringLiteral("ok"), true);
        reply(result);
    }
    else
        reply({{"ok", false}, {"error", QStringLiteral("unknown command")}});
}

void AddonTestDriver::waitFor(std::function<bool()> ready, int timeoutMs, std::function<void(bool)> done)
{
    endWait();
    // The wait can end inside one of its own callbacks, so it is deleted later
    // and a flag stops its remaining callbacks.
    auto alive = std::make_shared<bool>(true);
    m_waitAlive = alive;
    m_wait = new QObject(this);
    auto finish = [this, alive, ready, done](bool timedOut) {
        if (!*alive || (!timedOut && !ready()))
            return;
        endWait();
        done(!timedOut);
    };
    auto check = [finish] { finish(false); };
    if (m_summaries) {
        connect(m_summaries, &AddonPackageSummaries::stateChanged, m_wait, check);
        connect(m_summaries, &AddonPackageSummaries::loadingFinished, m_wait, check);
    }
    if (m_manager)
        connect(m_manager, &AddonManager::distributionBootstrapChanged, m_wait, check);
    if (m_discovery) {
        connect(m_discovery, &AddonDiscoveryService::changed, m_wait, check);
        connect(m_discovery, &AddonDiscoveryService::availabilityChanged, m_wait, check);
        connect(m_discovery, &AddonDiscoveryService::viewChanged, m_wait, check);
    }
    m_deadline = new QTimer(m_wait);
    m_deadline->setSingleShot(true);
    connect(m_deadline, &QTimer::timeout, m_wait, [finish] { finish(true); });
    m_deadline->start(timeoutMs);
    // The state can already be ready.
    QTimer::singleShot(0, m_wait, check);
}

void AddonTestDriver::endWait()
{
    if (m_waitAlive)
        *m_waitAlive = false;
    m_waitAlive.reset();
    if (m_deadline)
        m_deadline->stop();
    if (m_wait)
        m_wait->deleteLater();
    m_wait = nullptr;
    m_deadline = nullptr;
}

void AddonTestDriver::extensions()
{
    if (!m_summaries) {
        reply({{"ok", false}, {"error", QStringLiteral("no package summaries")}});
        return;
    }
    const QPointer<AddonPackageSummaries> summaries(m_summaries);
    m_summaries->refresh();
    waitFor([summaries] { return summaries && !summaries->loading(); }, 30000,
        [this, summaries](bool ready) {
            reply({{"ok", ready && summaries && summaries->isCurrent()},
                   {"ms", m_clock.elapsed()},
                   {"rows", summaries ? summaries->rows().size() : 0},
                   {"error", summaries ? summaries->error() : QString()}});
        });
}

void AddonTestDriver::install(const QString& directory)
{
    if (!m_manager) {
        reply({{"ok", false}, {"error", QStringLiteral("no add-on manager")}});
        return;
    }
    const QPointer<AddonManager> manager(m_manager);
    // Installation refuses a store that another owner holds. Wait for the
    // bundled preparation, then retry a busy store for a short time.
    waitFor([manager] { return !manager || !manager->distributionBootstrapInProgress(); }, 60000,
        [this, manager, directory](bool) {
            auto attempt = std::make_shared<std::function<void(int)>>();
            *attempt = [this, manager, directory, weak = std::weak_ptr<std::function<void(int)>>(attempt)](int tries) {
                if (!manager) {
                    reply({{"ok", false}, {"error", QStringLiteral("no add-on manager")}});
                    return;
                }
                const auto id = manager->inspectLocalPackage(directory).value(QStringLiteral("id")).toString();
                const bool installed = !id.isEmpty() && manager->installLocalPackage(directory, true)
                    && manager->setEnabled(id, true);
                const auto error = installed ? QString() : manager->errorMessage();
                if (!installed && tries < 50 && error.contains(QLatin1String("busy"))) {
                    if (const auto next = weak.lock())
                        QTimer::singleShot(100, this, [next, tries] { (*next)(tries + 1); });
                    return;
                }
                reply({{"ok", installed}, {"id", id}, {"ms", m_clock.elapsed()}, {"tries", tries + 1},
                       {"error", error}});
            };
            (*attempt)(0);
        });
}

// discover <package id> <query>: open the package's discovery source, load
// its roots, then search twice. The first call includes the worker start.
void AddonTestDriver::discover(const QString& argument)
{
    const auto space = argument.indexOf(QLatin1Char(' '));
    const auto package = space < 0 ? argument : argument.left(space);
    const auto query = space < 0 ? QStringLiteral("a") : argument.mid(space + 1);
    if (!m_discovery || package.isEmpty()) {
        reply({{"ok", false}, {"error", QStringLiteral("no discovery service or package")}});
        return;
    }
    const QPointer<AddonDiscoveryService> discovery(m_discovery);
    const auto display = discovery->acquireDisplay();
    auto result = std::make_shared<QVariantMap>();
    auto fail = [this, discovery, display, result](const QString& error) {
        if (discovery)
            discovery->releaseDisplay(display);
        result->insert(QStringLiteral("ok"), false);
        result->insert(QStringLiteral("error"), error);
        reply(*result);
    };
    auto sourceId = [discovery, package] {
        if (!discovery || !discovery->available())
            return QString();
        for (const auto& value : discovery->sources()) {
            const auto id = value.toMap().value(QStringLiteral("id")).toString();
            if (id.startsWith(package + QLatin1Char('/')) || id == package)
                return id;
        }
        return QString();
    };
    auto idle = [discovery](const QString& view) {
        if (!discovery)
            return true;
        const auto state = discovery->view(view);
        return state.isEmpty() || !state.value(QStringLiteral("busy")).toBool();
    };
    auto record = [discovery, result](const QString& view, const QString& key, qint64 ms) {
        const auto state = discovery ? discovery->view(view) : QVariantMap{};
        result->insert(key + QStringLiteral("Ms"), ms);
        result->insert(key + QStringLiteral("Items"), state.value(QStringLiteral("items")).toList().size());
        const auto error = state.value(QStringLiteral("error")).toString();
        if (!error.isEmpty())
            result->insert(key + QStringLiteral("Error"), error);
        return error.isEmpty() && state.value(QStringLiteral("available")).toBool();
    };
    waitFor([sourceId] { return !sourceId().isEmpty(); }, 30000,
        [=, this](bool ready) {
            if (!ready) { fail(QStringLiteral("the discovery source is not available")); return; }
            result->insert(QStringLiteral("availableMs"), m_clock.elapsed());
            const auto view = discovery->openSource(sourceId());
            if (view.isEmpty()) { fail(QStringLiteral("the source did not open")); return; }
            auto close = [discovery, view, display] {
                if (!discovery) return;
                discovery->closeView(view);
                discovery->releaseDisplay(display);
            };
            // openSource starts the roots request on the next event.
            const qint64 rootsStart = m_clock.elapsed();
            waitFor([discovery, view] { return discovery && discovery->view(view).value(QStringLiteral("items")).toList().size() > 0
                        && !discovery->view(view).value(QStringLiteral("busy")).toBool(); }, 30000,
                [=, this](bool rootsReady) {
                    if (!rootsReady || !record(view, QStringLiteral("roots"), m_clock.elapsed() - rootsStart)) {
                        close(); fail(QStringLiteral("the roots did not load")); return;
                    }
                    auto searchOnce = std::make_shared<std::function<void(int)>>();
                    *searchOnce = [=, this](int round) {
                        const qint64 start = m_clock.elapsed();
                        if (!discovery || !discovery->search(view, query)) {
                            QTimer::singleShot(0, this, [searchOnce] { *searchOnce = {}; });
                            close(); fail(QStringLiteral("the search did not start")); return;
                        }
                        waitFor([idle, view] { return idle(view); }, 30000, [=, this](bool searched) {
                            const auto key = round == 0 ? QStringLiteral("search") : QStringLiteral("repeatSearch");
                            if (!searched || !record(view, key, m_clock.elapsed() - start)) {
                                QTimer::singleShot(0, this, [searchOnce] { *searchOnce = {}; });
                                close(); fail(QStringLiteral("the search did not finish")); return;
                            }
                            if (round == 0) {
                                // Leave the search, so the repeat is a new request.
                                discovery->back(view);
                                waitFor([idle, view] { return idle(view); }, 30000,
                                    [searchOnce](bool) { (*searchOnce)(1); });
                                return;
                            }
                            QTimer::singleShot(0, this, [searchOnce] { *searchOnce = {}; });
                            result->insert(QStringLiteral("source"), discovery ? discovery->view(view).value(QStringLiteral("source")) : QVariant());
                            close();
                            result->insert(QStringLiteral("ok"), true);
                            result->insert(QStringLiteral("ms"), m_clock.elapsed());
                            reply(*result);
                        });
                    };
                    (*searchOnce)(0);
                });
        });
}

}
