// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <functional>

namespace vlc::addons {
struct PackageCandidate;

// The offline check of an executable release before it can be installed
// (install transaction step 5). The worker must compile and start the
// module, and a module that declares runtime.selfTest must answer
// {"type":"self-test"} with an "ok" result without any host call. The
// module gets no settings, secrets, network or playback. Packages without a
// runtime pass. Run on background work: it waits up to TimeLimitMs.
class AddonCandidateCheck final
{
public:
    static constexpr int TimeLimitMs = 15000;
    // cancelled is polled about every 50 ms on the calling thread.
    static bool run(const QString& workerExecutable, const PackageCandidate&, QString* error,
                    const std::function<bool()>& cancelled = {});
};
}
