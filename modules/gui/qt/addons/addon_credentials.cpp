// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_credentials.hpp"
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>
#include <QRegularExpression>
#include <memory>
#include <mutex>
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
bool validPackage(const QString& package)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)+$"));
    return package.size() <= 128 && pattern.match(package).hasMatch();
}

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
QHash<QString, QHash<QString, bool>> CredentialStore::presenceSnapshot() const
{
    QHash<QString, QHash<QString, bool>> result;
    for (auto it = m_session.cbegin(); it != m_session.cend(); ++it)
        result[it->packageId].insert(it.key(), !it->value.isEmpty());
    return result;
}
CredentialStore::Reader CredentialStore::snapshotReader() const
{
    struct Snapshot {
        explicit Snapshot(const CredentialStore& value) : store(value) {}
        ~Snapshot() { store.clearSession(); }
        std::mutex mutex;
        CredentialStore store;
    };
    const auto captured = std::make_shared<Snapshot>(*this);
    return [captured](const QString& package, const QString& reference) {
        std::lock_guard<std::mutex> lock(captured->mutex);
        return captured->store.read(package, reference);
    };
}
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
void CredentialStore::forgetPackage(const QString& package)
{
    if (!validPackage(package)) return;
    for (auto it = m_session.begin(); it != m_session.end();)
        if (it->packageId == package) { it->value.fill('\0'); it = m_session.erase(it); }
        else ++it;
}
bool CredentialStore::removePackage(const QString& package)
{
    if (!validPackage(package)) return false;
    forgetPackage(package);
#ifdef Q_OS_WIN
    const auto prefix = credentialName(package, QString{}).toStdWString();
    const auto filter = prefix + L"*";
    DWORD count = 0; PCREDENTIALW* entries = nullptr;
    if (!CredEnumerateW(filter.c_str(), 0, &count, &entries)) return GetLastError() == ERROR_NOT_FOUND;
    bool success = count <= 4096;
    if (success) for (DWORD i = 0; i < count; ++i) {
        const auto* item = entries[i];
        const auto target = QString::fromWCharArray(item->TargetName);
        if (item->Type != CRED_TYPE_GENERIC || !target.startsWith(QString::fromStdWString(prefix))) continue;
        if (!CredDeleteW(item->TargetName, CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND) success = false;
    }
    CredFree(entries); return success;
#elif defined(Q_OS_MACOS)
    const void* keys[] = {kSecClass, kSecAttrService, kSecReturnAttributes, kSecMatchLimit};
    const void* values[] = {kSecClassGenericPassword, CFSTR("VLC-addons"), kCFBooleanTrue, kSecMatchLimitAll};
    CFDictionaryRef query = CFDictionaryCreate(nullptr, keys, values, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFTypeRef result = nullptr;
    const auto status = SecItemCopyMatching(query, &result); CFRelease(query);
    if (status == errSecItemNotFound) return true;
    if (status != errSecSuccess || !result) return false;
    bool success = CFGetTypeID(result) == CFArrayGetTypeID();
    const auto entries = success ? static_cast<CFArrayRef>(result) : nullptr;
    if (success && CFArrayGetCount(entries) > 4096) success = false;
    const auto prefix = credentialName(package, QString{});
    if (success) for (CFIndex i = 0; i < CFArrayGetCount(entries); ++i) {
        const auto entry = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(entries, i));
        if (CFGetTypeID(entry) != CFDictionaryGetTypeID()) { success = false; continue; }
        const auto account = static_cast<CFStringRef>(CFDictionaryGetValue(entry, kSecAttrAccount));
        if (!account || CFGetTypeID(account) != CFStringGetTypeID()) { success = false; continue; }
        const auto length = CFStringGetLength(account);
        if (length > 1024) { success = false; continue; }
        QString text(length, Qt::Uninitialized);
        CFStringGetCharacters(account, CFRangeMake(0, length), reinterpret_cast<UniChar*>(text.data()));
        if (!text.startsWith(prefix)) continue;
        const void* removeKeys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
        const void* removeValues[] = {kSecClassGenericPassword, CFSTR("VLC-addons"), account};
        CFDictionaryRef remove = CFDictionaryCreate(nullptr, removeKeys, removeValues, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        const auto removed = SecItemDelete(remove); CFRelease(remove);
        if (removed != errSecSuccess && removed != errSecItemNotFound) success = false;
    }
    CFRelease(result); return success;
#elif defined(Q_OS_LINUX)
    if (!persistentAvailable()) return false;
    const auto executable = QStandardPaths::findExecutable(QStringLiteral("secret-tool"));
    QProcess process;
    process.start(executable, {QStringLiteral("clear"), QStringLiteral("application"), QStringLiteral("vlc-addons"),
        QStringLiteral("package"), package});
    if (!process.waitForStarted(1000)) return false;
    process.closeWriteChannel();
    if (!process.waitForFinished(2000)) { process.kill(); process.waitForFinished(1000); return false; }
    // libsecret reports no matching item as exit 1 with no diagnostic.
    return process.exitStatus() == QProcess::NormalExit && (process.exitCode() == 0
        || (process.exitCode() == 1 && process.readAllStandardError().isEmpty()));
#else
    return false;
#endif
}

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
    secretTool({QStringLiteral("store"), QStringLiteral("--label=Hakone add-on credential"),
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
