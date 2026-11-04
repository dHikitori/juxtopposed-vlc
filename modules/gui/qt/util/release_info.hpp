// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QJsonArray>
#include <QStringList>
#include <QUrl>
#include <optional>

namespace vlc::updates {

struct Version
{
    // Compiled identity for the installed build; explicit metadata for releases.
    // Tags alone always parse as generation zero.
    unsigned generation = 0;
    unsigned major = 0, minor = 0, patch = 0;
    QStringList prerelease;
    QString text;

    static std::optional<Version> parse(QString text);
    bool isPrerelease() const { return !prerelease.isEmpty(); }
    int compare(const Version& other) const;
};

struct Target
{
    QString os;
    QString arch;
    static Target current();
    QStringList packageSuffixes() const;
};

// Keep discovery metadata separate from download/installation. A future updater
// can consume the chosen asset after adding an authenticated update protocol.
struct Asset
{
    QString name;
    QUrl url;
    qint64 size = 0;
    QString digest; // GitHub metadata, not an independent publisher signature.
};

struct Release
{
    Version version;
    QString tag;
    QString notes;
    QUrl pageUrl;
    Asset asset;
    QString productName = QStringLiteral("Hakone");
    QString subtitle;
};

std::optional<Release> selectRelease(const QJsonArray& releases,
                                    const Version& installed, const Target& target);

} // namespace vlc::updates
