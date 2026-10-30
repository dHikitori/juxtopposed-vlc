// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "addon_process_launch.hpp"
#include <QDebug>
#include <QFileInfo>
#include <QProcess>
#include <atomic>
#ifdef Q_OS_WIN
# include <windows.h>
#endif

namespace vlc::addons {
namespace {
std::atomic<quint64> launches{0};
}

void prepareHelperLaunch(QProcess& process)
{
    const auto count = launches.fetch_add(1, std::memory_order_relaxed) + 1;
    // Visible in Tools > Messages at debug level, for the Windows cursor check.
    qDebug("Add-on helper launch %llu%s%s", static_cast<unsigned long long>(count),
        process.program().isEmpty() ? "" : ": ",
        qUtf8Printable(QFileInfo(process.program()).fileName()));
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->startupInfo->dwFlags |= STARTF_FORCEOFFFEEDBACK;
    });
#else
    Q_UNUSED(process);
#endif
}

quint64 helperLaunchCount() noexcept
{
    return launches.load(std::memory_order_relaxed);
}
}
