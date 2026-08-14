#include "AsyncTask.h"

AsyncTask::AsyncTask(
    Work work, OnComplete onCompleteCallback
) : m_OnCompleteCallback(std::move(onCompleteCallback))
{
    m_Thread = std::jthread([this, w = std::move(work)] (const std::stop_token& st) {
        m_ErrorOpt = w(st);
        m_Finished.store(true, std::memory_order_release);
    });
}

AsyncTask::~AsyncTask() {
    unbindCallbacks();
    // ~jthread does request_stop() + join() by itself
}

void AsyncTask::checkFinished() {
    if (m_Running && m_Finished.load(std::memory_order_acquire)) {
        m_Running = false;
        const bool wasCancelled = m_Thread.get_stop_token().stop_requested();
        if (m_Thread.joinable())
            m_Thread.join();

        // the callback may destroy this task (e.g. by starting the next one)
        auto errorOpt = std::move(m_ErrorOpt);
        const auto completeCallback = std::move(m_OnCompleteCallback);
        const auto deferredCancelCallback = std::move(m_DeferredCancelCallback);

        if (completeCallback)
            completeCallback(std::move(errorOpt), wasCancelled);

        if (wasCancelled && deferredCancelCallback)
            deferredCancelCallback();
    }
}

void AsyncTask::requestCancel(std::function<void()> deferredCallback) {
    if (m_Running) {
        m_DeferredCancelCallback = std::move(deferredCallback);
        m_Thread.request_stop();
    }
}

void AsyncTask::unbindCallbacks() {
    m_OnCompleteCallback = nullptr;
    m_DeferredCancelCallback = nullptr;
}
