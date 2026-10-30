// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QtGlobal>

class QProcess;

namespace vlc::addons {
// Call right before every add-on helper process starts. It counts the
// launch. On Windows it also turns off the startup-feedback cursor, which
// Windows otherwise shows over the whole desktop for each helper start.
void prepareHelperLaunch(QProcess&);
// Helper launches in this process so far.
quint64 helperLaunchCount() noexcept;
}
