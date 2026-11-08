// SPDX-License-Identifier: GPL-2.0-or-later
#include "background_watcher.hpp"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <memory>

namespace {
// What a watcher event has to change to be worth reporting. Hidden entries are
// left out, so Finder writing .DS_Store into a watched folder is not a change.
QByteArray snapshot(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists()) return QByteArrayLiteral("-");
    QByteArray result;
    const auto add = [&result](const QFileInfo& entry) {
        result += entry.fileName().toUtf8() + '\0' + (entry.isDir() ? 'd' : 'f') + '\0'
            + QByteArray::number(entry.size()) + '\0'
            + QByteArray::number(entry.lastModified().toMSecsSinceEpoch()) + '\n';
    };
    if (!info.isDir()) { add(info); return result; }
    result = QByteArrayLiteral("d\n");
    for (const auto& entry : QDir(path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System, QDir::Name))
        add(entry);
    return result;
}
}

BackgroundWatcher::BackgroundWatcher(QObject* context, std::function<void(const QString&)> changed)
{
    m_thread.setObjectName(QStringLiteral("vlc-watcher"));
    m_worker = new QObject;
    m_watcher = new QFileSystemWatcher(m_worker);
    // Moving the parent moves the watcher with it.
    m_worker->moveToThread(&m_thread);
    // The watcher emits on its thread; these connections queue to context.
    const QPointer<QObject> owner(context);
    // Only touched on the watcher thread. macOS can deliver events late: for
    // writes made before a path was added, and for paths already removed. A
    // path that is no longer watched, or whose snapshot still matches the one
    // taken when it was added or last reported, has nothing to report.
    m_snapshots = std::make_shared<QHash<QString, QByteArray>>();
    const auto report = [owner, changed = std::move(changed), snapshots = m_snapshots](const QString& path) {
        if (!owner) return;
        const auto it = snapshots->find(path);
        if (it == snapshots->end()) return;
        auto current = snapshot(path);
        if (current == *it) return;
        *it = std::move(current);
        QMetaObject::invokeMethod(owner, [owner, changed, path] { if (owner) changed(path); }, Qt::QueuedConnection);
    };
    QObject::connect(m_watcher, &QFileSystemWatcher::directoryChanged, m_watcher, report, Qt::DirectConnection);
    QObject::connect(m_watcher, &QFileSystemWatcher::fileChanged, m_watcher, report, Qt::DirectConnection);
    m_thread.start(QThread::LowPriority);
}

BackgroundWatcher::~BackgroundWatcher()
{
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] { delete worker; }, Qt::QueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void BackgroundWatcher::apply(const QStringList& add, const QStringList& remove)
{
    if (add.isEmpty() && remove.isEmpty()) return;
    QMetaObject::invokeMethod(m_worker, [watcher = m_watcher, snapshots = m_snapshots, add, remove] {
        if (!remove.isEmpty()) watcher->removePaths(remove);
        for (const auto& path : remove) snapshots->remove(path);
        for (const auto& path : add) snapshots->insert(path, snapshot(path));
        if (!add.isEmpty()) watcher->addPaths(add);
    }, Qt::QueuedConnection);
}

void BackgroundWatcher::setPaths(const QStringList& paths)
{
    const QSet<QString> next(paths.cbegin(), paths.cend());
    QStringList add, remove;
    for (const auto& path : next) if (!m_paths.contains(path)) add.append(path);
    for (const auto& path : std::as_const(m_paths)) if (!next.contains(path)) remove.append(path);
    m_paths = next;
    apply(add, remove);
}

void BackgroundWatcher::addPath(const QString& path)
{
    if (path.isEmpty() || m_paths.contains(path)) return;
    m_paths.insert(path);
    apply({path}, {});
}

void BackgroundWatcher::clear()
{
    if (m_paths.isEmpty()) return;
    apply({}, QStringList(m_paths.cbegin(), m_paths.cend()));
    m_paths.clear();
}
