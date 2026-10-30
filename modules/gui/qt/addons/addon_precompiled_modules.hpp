// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <QStringList>

namespace vlc::addons {
struct InstalledPackage;
// Precompiled worker modules (continuation plan finding F1). An entry is the
// native code that `vlc-addon-worker --precompile` made from one release's
// module with one worker build, and that code's SHA-256. The worker loads an
// entry only when the digest matches, and Wasmtime refuses code from another
// version or configuration. Otherwise the worker compiles from source.
//
// Entries live in the profile's "compiled" directory. They are a cache: no
// authority, setting or package state depends on them.
namespace AddonPrecompiledModules {
// Worker arguments for a start. Small file reads only. With no entry, this is
// the plain module path, and *missing is set so that the caller can start
// precompile() on joined work.
QStringList arguments(const QString& directory, const QString& executable,
                      const InstalledPackage&, bool* missing = nullptr);
// Joined work only: runs the worker once in precompile mode and publishes the
// entry. Other processes can write the same entry; the digest check keeps a
// mixed pair from loading.
// A set cancelled flag stops the compile within about 50 ms.
bool precompile(const QString& directory, const QString& executable, const InstalledPackage&,
                const std::atomic_bool* cancelled = nullptr);
// Starts precompile() on the application's joined work, once per entry.
void schedule(const QString& directory, const QString& executable, const InstalledPackage&);
}
}
