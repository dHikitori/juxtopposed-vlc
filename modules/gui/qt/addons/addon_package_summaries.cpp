// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_package_summaries.hpp"
#include "addon_manager.hpp"
#include "addon_catalogue_view.hpp"
#include <QPointer>
#include <QThread>

namespace vlc::addons {
AddonPackageSummaries::AddonPackageSummaries(AddonManager* manager,
    AddonStreamLifetime::Handle lifetime, QObject* parent)
    : AddonPackageSummaries([manager = QPointer<AddonManager>(manager)] {
        return manager ? manager->packageReader() : PackageSummaryReader{};
    }, std::move(lifetime), parent)
{
    if(manager && manager->displayCatalogue()) {
        attachDisplay(manager->displayCatalogue());
        connect(manager,&AddonManager::retiring,this,&AddonPackageSummaries::close,Qt::DirectConnection);
        connect(manager,&QObject::destroyed,this,&AddonPackageSummaries::close);
        return;
    }
    if (manager) {
        m_bootstrapManager=manager;
        connect(manager,&AddonManager::distributionBootstrapChanged,this,[this] {if(m_started)refresh();});
        connect(manager, &AddonManager::revisionChanged, this, [this] { if (m_started) refresh(); });
        connect(manager, &AddonManager::retiring, this, &AddonPackageSummaries::close, Qt::DirectConnection);
        connect(manager, &QObject::destroyed, this, &AddonPackageSummaries::close);
    } else close();
}
AddonPackageSummaries::AddonPackageSummaries(AddonCatalogueView& display,
    AddonStreamLifetime::Handle lifetime,QObject* parent)
    :AddonPackageSummaries(Capture{},std::move(lifetime),parent)
{ attachDisplay(&display); }
void AddonPackageSummaries::attachDisplay(AddonCatalogueView* display)
{
    m_displayMode=true;m_display=display;m_poll.stop();
    Capture retired;retired.swap(m_capture);
    connect(display,&AddonCatalogueView::catalogueChanged,this,&AddonPackageSummaries::scheduleDisplay);
    connect(display,&AddonCatalogueView::freshnessChanged,this,&AddonPackageSummaries::scheduleDisplay);
    connect(display,&QObject::destroyed,this,[this]{m_display=nullptr;scheduleDisplay();});
}
void AddonPackageSummaries::scheduleDisplay()
{
    if(m_closed || !m_started || m_displayScheduled)return;
    m_displayScheduled=true;
    QTimer::singleShot(0,this,[this]{
        m_displayScheduled=false;
        if(m_closed || !m_started)return;
        const QPointer<AddonPackageSummaries> alive(this);
        emit stateChanged();
        if(alive && !m_closed && !loading())emit loadingFinished(isCurrent());
    });
}
QVariantList AddonPackageSummaries::rows() const
{
    if(m_displayMode)return !m_closed && m_started && m_display?m_display->rows():QVariantList{};
    return isCurrent()?m_snapshot->rows:QVariantList{};
}
bool AddonPackageSummaries::loading() const noexcept
{ return m_displayMode?(!m_closed && m_started && m_display && m_display->loading()):m_loading; }
QString AddonPackageSummaries::error() const
{
    if(!m_displayMode)return m_error;
    if(m_closed || !m_started)return {};
    return m_display?m_display->error():tr("The extension list is not available.");
}
bool AddonPackageSummaries::retainedAvailable() const noexcept
{ return m_displayMode && !m_closed && m_started && m_lifetime.current() && m_display && m_display->retainedCatalogueCurrent(); }
bool AddonPackageSummaries::safeModeKnown() const
{ return m_displayMode && m_display && m_display->capabilities().value(QStringLiteral("safeModeKnown")).toBool(); }
bool AddonPackageSummaries::safeMode() const
{ return safeModeKnown() && m_display->capabilities().value(QStringLiteral("safeMode")).toBool(); }
AddonPackageSummaries::AddonPackageSummaries(Capture capture,
    AddonStreamLifetime::Handle lifetime, QObject* parent)
    : QObject(parent), m_capture(std::move(capture)), m_lifetime(std::move(lifetime)), m_jobs(m_lifetime)
{
    m_poll.setInterval(100);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        if (!m_lifetime.current()) { close(); return; }
        if (m_snapshot && !m_snapshot->authority.current()) refresh();
    });
    // No background read or polling until the view explicitly requests rows.
}
AddonPackageSummaries::~AddonPackageSummaries()
{
    // Destruction does not publish another intermediate view state.
    m_closed = true; m_poll.stop(); m_jobs.close();
}
bool AddonPackageSummaries::isCurrent() const noexcept
{
    if(m_displayMode)return !m_closed && m_started && m_lifetime.current() && m_display && m_display->isCurrent();
    return !m_closed && !m_loading && (!m_bootstrapManager || !m_bootstrapManager->distributionBootstrapInProgress())
        && m_lifetime.current() && m_snapshot
        && m_generation == m_publishedGeneration && m_snapshot->authority.current();
}
void AddonPackageSummaries::publish(quint64 generation, bool terminal, bool success)
{
    const QPointer<AddonPackageSummaries> alive(this);
    emit stateChanged();
    if (!alive || m_generation != generation) return;
    if (terminal) emit loadingFinished(success);
}
void AddonPackageSummaries::refresh()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed) return;
    if (!m_lifetime.current()) { close(); return; }
    if(m_displayMode) {
        m_started=true;const auto display=m_display;const QPointer<AddonPackageSummaries> alive(this);
        if(display)display->inputsChanged();
        if(alive && !m_closed)scheduleDisplay();
        return;
    }
    if (!m_started) m_started = true;
    const auto generation = ++m_generation;
    m_pending = true; m_loading = true; m_error.clear();
    std::optional<PackageSummarySnapshot> retired; retired.swap(m_snapshot);
    if(m_bootstrapManager && m_bootstrapManager->distributionBootstrapInProgress()) {
        m_poll.stop();if(m_jobs.busy())m_jobs.cancel();publish(generation,false,false);return;
    }
    if(!m_poll.isActive())m_poll.start();
    if (m_jobs.busy()) m_jobs.cancel(); else schedule();
    publish(generation, false, false);
}
void AddonPackageSummaries::schedule()
{
    if (m_scheduled || m_closed) return;
    m_scheduled = true;
    QTimer::singleShot(0, this, [this] { m_scheduled = false; start(); });
}
void AddonPackageSummaries::start()
{
    if (m_closed || !m_pending || m_jobs.busy()) return;
    if(m_bootstrapManager && m_bootstrapManager->distributionBootstrapInProgress()) {m_loading=true;m_poll.stop();return;}
    const auto generation = m_generation;
    const auto capture = m_capture;
    const QPointer<AddonPackageSummaries> alive(this);
    PackageSummaryReader reader;
    try { if (capture) reader = capture(); } catch (...) {}
    if (!alive || m_closed || m_generation != generation) return;
    m_pending = false;
    if (!reader || !m_lifetime.current()) {
        m_loading = false; m_error = tr("The app cannot read the add-on list.");
        publish(generation, true, false); return;
    }
    auto result = std::make_shared<std::optional<PackageSummarySnapshot>>();
    const bool submitted = m_jobs.submit([reader = std::move(reader), result] {
        *result = reader();
    }, [this, generation, result](auto state) { completed(generation, result, state); });
    if (!alive || m_closed || m_generation != generation) return;
    if (!submitted) {
        m_loading = false; m_error = tr("Wait for the other add-on work to end, then try again.");
        publish(generation, true, false);
    }
}
void AddonPackageSummaries::completed(quint64 generation,
    const std::shared_ptr<std::optional<PackageSummarySnapshot>>& result, AddonRepositoryJobs::Result state)
{
    if (m_closed) return;
    if (generation != m_generation) { if (m_pending) schedule(); return; }
    m_loading = false;
    if (state == AddonRepositoryJobs::Result::Finished && *result && (*result)->authority.current() && m_lifetime.current()) {
        m_snapshot.swap(*result); m_publishedGeneration = generation; m_error.clear();
        publish(generation, true, true);
    } else {
        m_error = tr("The add-on list changed, or the app cannot read it. Try again.");
        publish(generation, true, false);
    }
}
void AddonPackageSummaries::close()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed) return;
    m_closed = true; const auto generation = ++m_generation;
    m_pending = false; m_loading = false; m_error.clear();
    std::optional<PackageSummarySnapshot> retired; retired.swap(m_snapshot);
    Capture capture; capture.swap(m_capture);m_bootstrapManager=nullptr;
    m_poll.stop(); m_jobs.close();
    if(m_display)disconnect(m_display,nullptr,this,nullptr);
    m_display=nullptr;
    publish(generation, false, false);
}
}
