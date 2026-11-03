// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "addon_process_launch.hpp"
#include <QFileInfo>
#include <QProcess>
#include <atomic>
#include <memory>
#include <mutex>
#ifdef Q_OS_WIN
# include <windows.h>
#endif

namespace vlc::addons {
namespace {
std::atomic<quint64> launches{0};
std::mutex observerLock;
std::shared_ptr<const std::function<void(quint64, const QString&)>> observer;
}

void prepareHelperLaunch(QProcess& process)
{
    const auto count = launches.fetch_add(1, std::memory_order_relaxed) + 1;
    std::shared_ptr<const std::function<void(quint64, const QString&)>> report;
    {
        std::lock_guard<std::mutex> guard(observerLock);
        report = observer;
    }
    if (report) (*report)(count, QFileInfo(process.program()).fileName());
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->startupInfo->dwFlags |= STARTF_FORCEOFFFEEDBACK;
    });
#else
    Q_UNUSED(process);
#endif
}

void setHelperLaunchObserver(std::function<void(quint64, const QString&)> report)
{
    std::lock_guard<std::mutex> guard(observerLock);
    observer = report ? std::make_shared<const std::function<void(quint64, const QString&)>>(std::move(report)) : nullptr;
}

quint64 helperLaunchCount() noexcept
{
    return launches.load(std::memory_order_relaxed);
}
}
