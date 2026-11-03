// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "release_info.hpp"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>

class GithubReleaseService : public QObject
{
    Q_OBJECT
public:
    struct Options
    {
        QUrl endpoint = QUrl(QStringLiteral("https://api.github.com/repos/hakone-org/hakone/releases"));
        int timeoutMs = 20000;
        qint64 maxJsonBytes = 4 * 1024 * 1024;
        int maxPages = 100;
    };
    explicit GithubReleaseService(QObject* parent = nullptr);
    explicit GithubReleaseService(const Options& options, QObject* parent = nullptr);
    ~GithubReleaseService() override;
    void check(const vlc::updates::Version& installed, const vlc::updates::Target& target);
    const std::optional<vlc::updates::Release>& release() const { return m_release; }
signals:
    void finished(bool success);
private:
    void fetchPage(int page);
    void complete(bool success);
    Options m_options;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_body;
    vlc::updates::Version m_installed;
    vlc::updates::Target m_target;
    std::optional<vlc::updates::Release> m_release;
    bool m_busy = false;
    bool m_tooLarge = false;
};
