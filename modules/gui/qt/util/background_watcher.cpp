// SPDX-License-Identifier: GPL-2.0-or-later
#include "background_watcher.hpp"
#include <QFileSystemWatcher>

BackgroundWatcher::BackgroundWatcher(QObject* context, std::function<void(const QString&)> changed)
{
    m_thread.setObjectName(QStringLiteral("vlc-watcher"));
    m_worker = new QObject;
    m_watcher = new QFileSystemWatcher(m_worker);
    // Moving the parent moves the watcher with it.
    m_worker->moveToThread(&m_thread);
    // The watcher emits on its thread; these connections queue to context.
    const QPointer<QObject> owner(context);
    const auto report = [owner, changed = std::move(changed)](const QString& path) {
        if (!owner) return;
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
    QMetaObject::invokeMethod(m_worker, [watcher = m_watcher, add, remove] {
        if (!remove.isEmpty()) watcher->removePaths(remove);
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
