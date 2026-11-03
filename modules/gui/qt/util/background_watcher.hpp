// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_BACKGROUND_WATCHER_HPP
#define VLC_QT_BACKGROUND_WATCHER_HPP

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <functional>

class QFileSystemWatcher;

// A QFileSystemWatcher on its own thread. Adding or removing a watch opens or
// closes the path, which on a network share can take seconds; here it never
// runs on the caller's thread. Changes are reported to `changed` on the
// thread of `context`. Use from one thread.
class BackgroundWatcher final
{
public:
    BackgroundWatcher(QObject* context, std::function<void(const QString&)> changed);
    ~BackgroundWatcher();
    BackgroundWatcher(const BackgroundWatcher&) = delete;
    BackgroundWatcher& operator=(const BackgroundWatcher&) = delete;

    // Watch exactly these paths; only the difference is applied.
    void setPaths(const QStringList&);
    void addPath(const QString&);
    void clear();
    bool contains(const QString& path) const { return m_paths.contains(path); }
    bool isEmpty() const { return m_paths.isEmpty(); }

private:
    void apply(const QStringList& add, const QStringList& remove);
    QThread m_thread;
    QObject* m_worker = nullptr;
    QFileSystemWatcher* m_watcher = nullptr;
    QSet<QString> m_paths;
};

#endif
