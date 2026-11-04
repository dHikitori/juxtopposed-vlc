// SPDX-License-Identifier: GPL-2.0-or-later
#include "github_release_service.hpp"

#include <QJsonDocument>
#include <QTimer>
#include <QUrlQuery>

GithubReleaseService::GithubReleaseService(QObject* parent)
    : GithubReleaseService(Options{}, parent)
{
}

GithubReleaseService::GithubReleaseService(const Options& options, QObject* parent)
    : QObject(parent), m_options(options)
{
}

GithubReleaseService::~GithubReleaseService()
{
    if (m_reply)
    {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
    }
}

void GithubReleaseService::check(const vlc::updates::Version& installed,
                               const vlc::updates::Target& target)
{
    if (m_busy)
        return;
    m_busy = true;
    m_release.reset();
    m_installed = installed;
    m_target = target;
    fetchPage(1);
}

void GithubReleaseService::complete(bool success)
{
    if (!success)
        m_release.reset();
    m_busy = false;
    emit finished(success);
}

void GithubReleaseService::fetchPage(int page)
{
    auto url = m_options.endpoint;
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("per_page"), QStringLiteral("100"));
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("User-Agent", QByteArray("Hakone/") + m_installed.text.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    auto* reply = m_network.get(request);
    m_reply = reply;
    m_body.clear();
    m_tooLarge = false;
    // An absolute deadline also covers slow/trickling responses.
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    deadline->start(m_options.timeoutMs);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        const auto remaining = m_options.maxJsonBytes - m_body.size();
        m_body += reply->read(qMax<qint64>(0, remaining) + 1);
        if (m_body.size() > m_options.maxJsonBytes)
        {
            m_tooLarge = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, page, deadline] {
        deadline->stop();
        const bool ok = !m_tooLarge && reply->error() == QNetworkReply::NoError &&
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        reply->deleteLater();
        m_reply = nullptr;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(m_body, &error);
        if (!ok || error.error != QJsonParseError::NoError || !document.isArray())
        {
            complete(false);
            return;
        }
        const auto entries = document.array();
        const auto candidate = vlc::updates::selectRelease(entries, m_installed, m_target);
        if (candidate && (!m_release || candidate->version.compare(m_release->version) > 0))
            m_release = candidate;
        if (entries.size() < 100)
            complete(true);
        else if (page >= m_options.maxPages)
            complete(false); // Never report a partial search as up to date.
        else
            fetchPage(page + 1);
    });
}
