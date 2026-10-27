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

#include "mlthreadpool.hpp"

#include <QMutexLocker>
#include <QThread>
#include <QOperatingSystemVersion>

// The pool owns this wrapper through the end of run(). The worker reference
// keeps the QObject alive even if its queued completion is handled immediately.
class MLThreadPoolTask : public QRunnable
{
public:
    MLThreadPoolTask(ThreadRunner* pool, RunOnThreadBaseRunner* task) : m_pool(pool), m_task(task) { m_task->retain(); }
    ~MLThreadPoolTask() override { m_task->release(); }
    void run() override {
        { QMutexLocker lock(&m_pool->m_lock); m_task->m_queuedWrapper = nullptr; }
        m_task->run();
    }
private:
    ThreadRunner* m_pool;
    RunOnThreadBaseRunner* m_task;
};

ThreadRunner::ThreadRunner()
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 9, 0))
    // This is currently only applicable on Windows (10 RS3) and Darwin, as of Qt 6.11:
    m_threadPool.setServiceLevel(QThread::QualityOfService::Eco);
#endif

    // Even if we use energy efficient cores, we still need to limit the thread count,
    // because not all processors have energy efficient cores and it is assumed that
    // in that case all cores would be available:

    m_threadPool.setMaxThreadCount(std::clamp(QThread::idealThreadCount() - 1, 1, 4));
}

ThreadRunner::~ThreadRunner()
{
    m_threadPool.waitForDone();
    assert(m_objectTasks.empty());
    assert(m_runningTasks.empty());
}

void ThreadRunner::setMaxThreadCount(size_t threadCount)
{
    m_threadPool.setMaxThreadCount(threadCount);
}


bool ThreadRunner::start(RunOnThreadBaseRunner* task, const char* queue)
{
    if (queue == nullptr)
    {
        task->m_queuedWrapper = new MLThreadPoolTask(this, task);
        m_threadPool.start(task->m_queuedWrapper);
    }
    else
    {
        QMutexLocker lock(&m_serialTaskLock);
        if (m_serialTasks.contains(queue))
        {
            m_serialTasks[queue].push_back(task);
        }
        else
        {
            m_serialTasks[queue] = QQueue<RunOnThreadBaseRunner*>();
            m_serialTasks[queue].push_back(task);
            if (!processQueueLocked(queue)) { m_serialTasks.remove(queue); return false; }
        }
    }
    return true;
}

bool ThreadRunner::tryTake(RunOnThreadBaseRunner* task)
{
    if (task->m_queuedWrapper) {
        if (!m_threadPool.tryTake(task->m_queuedWrapper)) return false;
        delete task->m_queuedWrapper;
        task->m_queuedWrapper = nullptr;
        return true;
    }

    {
        QMutexLocker lock(&m_serialTaskLock);
        for (auto queueIt = m_serialTasks.begin(); queueIt != m_serialTasks.end(); ++queueIt)
        {
            auto& queue = queueIt.value();
            auto taskIt = std::find(queue.begin(), queue.end(), task);
            if (taskIt != queue.end())
            {
                queue.erase(taskIt);
                // The active executor still owns this queue. Removing its last
                // pending task must not allow a second executor to start.
                return true;
            }
        }
    }
    return false;
}


RunOnThreadBaseRunner* ThreadRunner::getNextTaskFromQueue(const QString& queueName)
{
    QMutexLocker lock(&m_serialTaskLock);
    auto& queue = m_serialTasks[queueName];
    if (queue.empty())
    {
        m_serialTasks.remove(queueName);
        return nullptr;
    }
    auto task = queue.front();
    task->retain();
    queue.pop_front();
    return task;
}

bool ThreadRunner::processQueueLocked(const QString& queueName)
{
    if (!m_serialTasks.contains(queueName))
        return true;

    const auto taskId = nextMLTaskId();
    if (!taskId) return false;
    struct Ctx{};
    auto runnable = new RunOnThreadRunner<Ctx>(taskId, this,
        [this, queueName](Ctx&){
            auto task = getNextTaskFromQueue(queueName);
            if (!task)
            {
                return;
            }
            task->run();
            task->release();
        },
        [this, queueName](quint64, const Ctx&){
             QMutexLocker tasksLock(&m_lock);
             QMutexLocker lock(&m_serialTaskLock);
             if (!m_shuttingDown && !processQueueLocked(queueName))
                 QMetaObject::invokeMethod(this,&ThreadRunner::destroy,Qt::QueuedConnection);
        });
    connect(runnable, &RunOnThreadBaseRunner::done, this, &ThreadRunner::runOnThreadDone);
    m_runningTasks.insert(taskId, runnable);
    m_objectTasks.insert(this, taskId);

    runnable->m_queuedWrapper = new MLThreadPoolTask(this, runnable);
    m_threadPool.start(runnable->m_queuedWrapper);
    return true;
}

void ThreadRunner::destroy()
{
    QMutexLocker locker{&m_lock};
    m_shuttingDown = true;
    //try to cancel as many tasks as possible
    for (auto taskIt = m_objectTasks.begin(); taskIt != m_objectTasks.end(); /**/)
    {
        const QObject* object = taskIt.key();
        quint64 key = taskIt.value();
        auto task = m_runningTasks.value(key, nullptr);
        if (tryTake(task))
        {
            task->release();
            m_runningTasks.remove(key);
            taskIt = m_objectTasks.erase(taskIt);
            if (m_objectTasks.count(object) == 0)
                disconnect(object, &QObject::destroyed, this, &ThreadRunner::runOnThreadTargetDestroyed);
        }
        else
            ++taskIt;
    }

    if (m_runningTasks.empty())
    {
        deleteLater();
    }
}

void ThreadRunner::cancelTask(const QObject* object, quint64 taskId)
{
    assert(taskId != 0);
    QMutexLocker locker{&m_lock};

    auto task = m_runningTasks.value(taskId, nullptr);
    if (!task)
        return;
    task->cancel();
    tryTake(task);
    task->release();
    m_runningTasks.remove(taskId);
    m_objectTasks.remove(object, taskId);
    if (m_objectTasks.count(object) == 0)
        disconnect(object, &QObject::destroyed, this, &ThreadRunner::runOnThreadTargetDestroyed);
}

void ThreadRunner::runOnThreadDone(RunOnThreadBaseRunner* runner, quint64 target, const QObject* object, int status)
{
    QMutexLocker locker{&m_lock};
    if (!m_runningTasks.contains(target))
    {
        return;
    }

    const auto owner = std::shared_ptr<RunOnThreadBaseRunner>(runner, [](auto* task) { task->release(); });

    m_runningTasks.remove(target);
    m_objectTasks.remove(object, target);
    if (m_objectTasks.count(object) == 0)
        disconnect(object, &QObject::destroyed, this, &ThreadRunner::runOnThreadTargetDestroyed);

    if (m_shuttingDown)
    {
        if (m_runningTasks.empty())
            deleteLater();
        return;
    }

    if (status == ML_TASK_STATUS_SUCCEED)
    {
        if (object->thread() == this->thread())
        {
            locker.unlock();
            owner->runUICallback();
        }
        else
        {
            //run the callback in the object thread
            QMetaObject::invokeMethod(
                const_cast<QObject*>(object),
                [owner](){
                    owner->runUICallback();
                }
            );
        }
    }
}

void ThreadRunner::runOnThreadTargetDestroyed(QObject * object)
{
    QMutexLocker locker{&m_lock};
    if (m_objectTasks.contains(object))
    {
        for (auto taskId : m_objectTasks.values(object))
        {
            auto task = m_runningTasks.value(taskId, nullptr);
            assert(task);
            tryTake(task);
            task->cancel();
            task->release();
            m_runningTasks.remove(taskId);
        }
        m_objectTasks.remove(object);
        //no need to disconnect QObject::destroyed, as object is currently being destroyed
    }
}
