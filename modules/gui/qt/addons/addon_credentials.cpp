// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_credentials.hpp"
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>
#include <QRegularExpression>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#elif defined(Q_OS_MACOS)
#include <Security/Security.h>
#endif

namespace vlc::addons {
namespace {
bool validReference(const QString& reference)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-f0-9-]{36}$"));
    return pattern.match(reference).hasMatch();
}
QString credentialName(const QString& package, const QString& reference)
{ return QStringLiteral("VLC/addons/%1/%2").arg(package, reference); }

#if defined(Q_OS_LINUX)
QByteArray secretTool(const QStringList& arguments, const QByteArray& input, bool* success)
{
    *success = false;
    const auto executable = QStandardPaths::findExecutable(QStringLiteral("secret-tool"));
    if (executable.isEmpty()) return {};
    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(1000)) return {};
    if (!input.isEmpty()) process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(2000)) { process.kill(); process.waitForFinished(1000); return {}; }
    *success = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    const auto result = process.readAllStandardOutput();
    return result.size() <= 8192 ? result : QByteArray{};
}
#endif
}

bool CredentialStore::persistentAvailable()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return true;
#elif defined(Q_OS_LINUX)
    return !QStandardPaths::findExecutable(QStringLiteral("secret-tool")).isEmpty()
        && !qEnvironmentVariableIsEmpty("DBUS_SESSION_BUS_ADDRESS");
#else
    return false;
#endif
}
CredentialStore::Created CredentialStore::create(const QString& package, const QString&, const QByteArray& value, bool allowSessionOnly)
{
    if (value.isEmpty() || value.size() > 4096 || value.contains('\r') || value.contains('\n') || value.contains('\0')) return {};
    const auto reference = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const bool persistent = !allowSessionOnly && persistentAvailable() && platformWrite(package, reference, value);
    if (!persistent && !allowSessionOnly) return {};
    m_session.insert(reference, {package, value, persistent});
    return {reference, persistent};
}

QByteArray CredentialStore::read(const QString& package, const QString& reference) const
{
    if (!validReference(reference)) return {};
    const auto it = m_session.constFind(reference);
    if (it != m_session.cend()) return it->packageId == package ? it->value : QByteArray{};
    const auto value = platformRead(package, reference);
    m_session.insert(reference, {package, value, true});
    return value;
}
bool CredentialStore::present(const QString& package, const QString& reference) const
{ return !read(package, reference).isEmpty(); }
bool CredentialStore::sessionOnly(const QString& reference) const
{ const auto it = m_session.constFind(reference); return it != m_session.cend() && !it->persistent; }
void CredentialStore::remove(const QString& package, const QString& reference)
{
    if (!validReference(reference)) return;
    const auto it = m_session.find(reference);
    if (it != m_session.end() && it->packageId != package) return;
    if (it != m_session.end()) { it->value.fill('\0'); m_session.erase(it); }
    platformRemove(package, reference);
}
void CredentialStore::clearSession()
{ for (auto& secret : m_session) secret.value.fill('\0'); m_session.clear(); }

bool CredentialStore::platformWrite(const QString& package, const QString& reference, const QByteArray& value)
{
#ifdef Q_OS_WIN
    auto target = credentialName(package, reference).toStdWString();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.CredentialBlobSize = DWORD(value.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(value.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW(&credential, 0);
#elif defined(Q_OS_MACOS)
    const auto account = credentialName(package, reference).toUtf8();
    return SecKeychainAddGenericPassword(nullptr, 10, "VLC-addons", quint32(account.size()), account.constData(),
        quint32(value.size()), value.constData(), nullptr) == errSecSuccess;
#elif defined(Q_OS_LINUX)
    bool success = false;
    secretTool({QStringLiteral("store"), QStringLiteral("--label=VLC add-on credential"),
        QStringLiteral("application"), QStringLiteral("vlc-addons"), QStringLiteral("package"), package,
        QStringLiteral("reference"), reference}, value, &success);
    return success;
#else
    Q_UNUSED(package); Q_UNUSED(reference); Q_UNUSED(value); return false;
#endif
}

QByteArray CredentialStore::platformRead(const QString& package, const QString& reference)
{
#ifdef Q_OS_WIN
    const auto target = credentialName(package, reference).toStdWString(); PCREDENTIALW credential = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) return {};
    QByteArray result(reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize);
    CredFree(credential); return result;
#elif defined(Q_OS_MACOS)
    const auto account = credentialName(package, reference).toUtf8(); quint32 size = 0; void* data = nullptr;
    if (SecKeychainFindGenericPassword(nullptr, 10, "VLC-addons", quint32(account.size()), account.constData(), &size, &data, nullptr) != errSecSuccess) return {};
    QByteArray result(static_cast<const char*>(data), size); SecKeychainItemFreeContent(nullptr, data); return result;
#elif defined(Q_OS_LINUX)
    bool success = false;
    auto value = secretTool({QStringLiteral("lookup"), QStringLiteral("application"), QStringLiteral("vlc-addons"),
        QStringLiteral("package"), package, QStringLiteral("reference"), reference}, {}, &success);
    if (value.endsWith('\n')) value.chop(1);
    return success ? value : QByteArray{};
#else
    Q_UNUSED(package); Q_UNUSED(reference); return {};
#endif
}
void CredentialStore::platformRemove(const QString& package, const QString& reference)
{
#ifdef Q_OS_WIN
    const auto target = credentialName(package, reference).toStdWString(); CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
#elif defined(Q_OS_MACOS)
    const auto account = credentialName(package, reference).toUtf8(); SecKeychainItemRef item = nullptr;
    if (SecKeychainFindGenericPassword(nullptr, 10, "VLC-addons", quint32(account.size()), account.constData(), nullptr, nullptr, &item) == errSecSuccess) {
        SecKeychainItemDelete(item); CFRelease(item);
    }
#elif defined(Q_OS_LINUX)
    bool success = false;
    secretTool({QStringLiteral("clear"), QStringLiteral("application"), QStringLiteral("vlc-addons"),
        QStringLiteral("package"), package, QStringLiteral("reference"), reference}, {}, &success);
#else
    Q_UNUSED(package); Q_UNUSED(reference);
#endif
}
}
