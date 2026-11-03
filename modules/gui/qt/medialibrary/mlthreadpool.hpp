/*****************************************************************************
 * Copyright (C) 2021 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef MLTHREADPOOL_HPP
#define MLTHREADPOOL_HPP

#include <QObject>
#include <QRunnable>
#include <QQueue>
#include <QMap>
#include <QThreadPool>
#include <QMutex>
#include <atomic>
#include <memory>
#include "mltaskid.hpp"

class RunOnThreadBaseRunner;

class ThreadRunner : public QObject
{
    Q_OBJECT

public:
    enum MLTaskStatus {
        ML_TASK_STATUS_SUCCEED,
        ML_TASK_STATUS_CANCELED
    };

    ThreadRunner();
    ~ThreadRunner();

    void setMaxThreadCount(size_t threadCount);

    void destroy();
    void cancelTask(const QObject* object, quint64 taskId);

    template<typename Ctx>
    quint64 runOnThread(const QObject* obj,
                          std::function<void (Ctx&)> mlFun,
                          std::function<void (quint64 taskId, Ctx&)> uiFun,
                          const char* queue = nullptr);

private slots:
    void runOnThreadDone(RunOnThreadBaseRunner* runner, quint64 target, const QObject* object, int status);
    void runOnThreadTargetDestroyed(QObject * object);


private:
    /**
     * @brief start enqueue a QRunnable to be executed on the threadpool
     * @param task is the task to enqueue
     * @param queue, the name of the queue, all task with the same queue name will be
     * ran sequentially, if queue is null, task will be run without additional specific
     * constraint (like on QThreadPool)
     */
    bool start(RunOnThreadBaseRunner* task, const char* queue = nullptr);

    /**
     * @brief tryTake atempt to the specified task from the queue if the task has not started
     * @return true if the task has been removed from the queue and was not started yet
     */
    bool tryTake(RunOnThreadBaseRunner* task);

    RunOnThreadBaseRunner* getNextTaskFromQueue(const QString& queueName);
    bool processQueueLocked(const QString& queueName);

private:
    QThreadPool m_threadPool;

    friend class MLThreadPoolSerialTask;
    friend class MLThreadPoolTask;
    QMutex m_serialTaskLock;
    QMap<QString, QQueue<RunOnThreadBaseRunner*>> m_serialTasks;

    bool m_shuttingDown = false;
    QMap<quint64, RunOnThreadBaseRunner*> m_runningTasks;
    QMultiMap<const QObject*, quint64> m_objectTasks;
    QMutex m_lock;
};

class RunOnThreadBaseRunner : public QObject, public QRunnable
{
    Q_OBJECT
public:
    virtual ~RunOnThreadBaseRunner() = default;
    virtual void runUICallback() = 0;
    virtual void cancel() = 0;
    void retain() { m_references.fetch_add(1, std::memory_order_relaxed); }
    void release() {
        if (m_references.fetch_sub(1, std::memory_order_acq_rel) == 1) deleteLater();
    }
signals:
    void done(RunOnThreadBaseRunner* runner, quint64 target, const QObject* object, int status);
private:
    friend class ThreadRunner;
    friend class MLThreadPoolTask;
    std::atomic_uint m_references {1}; // owner, plus any executing pool wrapper
    QRunnable* m_queuedWrapper = nullptr; // protected by ThreadRunner::m_lock
};

template<typename Ctx>
class RunOnThreadRunner : public RunOnThreadBaseRunner {
public:
    RunOnThreadRunner(
        quint64 taskId,
        const QObject* obj,
        std::function<void (Ctx&)> mlFun,
        std::function<void (quint64, Ctx&)> uiFun
        )
        : RunOnThreadBaseRunner()
        , m_taskId(taskId)
        , m_obj(obj)
        , m_mlFun(mlFun)
        , m_uiFun(uiFun)
    {
        setAutoDelete(false);
    }

    void run() override
    {
        if (m_canceled)
        {
            emit done(this, m_taskId, m_obj, ThreadRunner::ML_TASK_STATUS_CANCELED);
            return;
        }
        m_mlFun(m_ctx);
        emit done(this, m_taskId, m_obj, ThreadRunner::ML_TASK_STATUS_SUCCEED);
    }

    //called from UI thread
    void runUICallback() override
    {
        m_uiFun(m_taskId, m_ctx);
    }

    void cancel() override
    {
        m_canceled = true;
    }
private:
    std::atomic_bool m_canceled {false};
    quint64 m_taskId;
    Ctx m_ctx; //default constructed
    const QObject* m_obj = nullptr;
    const std::function<void (Ctx&)> m_mlFun;
    const std::function<void (quint64, Ctx&)> m_uiFun;
};

template<typename Ctx>
quint64 ThreadRunner::runOnThread(const QObject* obj,
                                      std::function<void (Ctx&)> mlFun,
                                      std::function<void (quint64 taskId, Ctx&)> uiFun,
                                      const char* queue)
{
    QMutexLocker locker{&m_lock};

    if (m_shuttingDown)
        return 0;

    const auto taskId = nextMLTaskId();
    if (!taskId) return 0;
    auto runnable = new RunOnThreadRunner<Ctx>(taskId, obj, mlFun, uiFun);
    connect(runnable, &RunOnThreadBaseRunner::done, this, &ThreadRunner::runOnThreadDone);
    connect(obj, &QObject::destroyed, this, &ThreadRunner::runOnThreadTargetDestroyed, Qt::DirectConnection);
    m_runningTasks.insert(taskId, runnable);
    m_objectTasks.insert(obj, taskId);
    if (!start(runnable, queue)) {
        m_runningTasks.remove(taskId);
        m_objectTasks.remove(obj,taskId);
        if (m_objectTasks.count(obj)==0)
            disconnect(obj,&QObject::destroyed,this,&ThreadRunner::runOnThreadTargetDestroyed);
        runnable->release();
        return 0;
    }
    return taskId;
}

#endif // MLTHREADPOOL_HPP
