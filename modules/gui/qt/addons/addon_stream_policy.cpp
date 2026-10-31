// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_stream_policy.hpp"
#include "addon_public_network_policy.hpp"

namespace vlc::addons {
bool AddonStreamPolicy::url(const QUrl& value)
{
    if (!value.isValid() || value.scheme() != QLatin1String("https") || value.host().isEmpty() || value.port(443) != 443
        || (!value.userInfo().isEmpty() || value.authority().contains(QLatin1Char('@'))) || value.authority(QUrl::FullyEncoded).contains(QLatin1Char('@'))
        || value.hasFragment() || value.toEncoded().size() > 8192 || value.host().size() > 253) return false;
    for (const auto& part : {value.path(QUrl::FullyDecoded),value.query(QUrl::FullyDecoded)})
        for (const auto c : part) if (c.unicode() < 32 || (c.unicode() >= 127 && c.unicode() <= 159) || c == QLatin1Char('\\')) return false;
    return !QHostAddress(value.host()).isNull() || !QUrl::toAce(value.host()).isEmpty();
}
bool AddonStreamPolicy::publicAddress(const QHostAddress& address)
{ return AddonPublicNetworkPolicy::address(address); }
QByteArray AddonStreamPolicy::origin(const QUrl& value)
{ const QHostAddress literal(value.host()); return literal.isNull() ? QUrl::toAce(value.host()).toLower() : literal.toString().toLatin1(); }
std::optional<AddonStreamPolicy::Plan> AddonStreamPolicy::plan(const QUrl& value,const QList<QHostAddress>& addresses)
{
    if (!url(value) || addresses.isEmpty() || addresses.size() > 32) return {};
    for (const auto& address : addresses) if (!publicAddress(address)) return {};
    auto selected = addresses.first();
    for (const auto& address : addresses) if (address.protocol() == QAbstractSocket::IPv4Protocol) { selected = address; break; }
    const auto hostname = QString::fromLatin1(origin(value));
    QByteArray host = hostname.toLatin1(); if (host.contains(':')) host = '[' + host + ']';
    QByteArray target = value.path(QUrl::FullyEncoded).toUtf8(); if (target.isEmpty()) target = "/";
    if (value.hasQuery()) target += '?' + value.query(QUrl::FullyEncoded).toUtf8();
    for (const unsigned char c : target) if (c <= 32 || c >= 127 || c == '\\') return {};
    return Plan{selected,hostname,host,target};
}
}
