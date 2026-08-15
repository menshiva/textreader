#include "AsyncTask.h"
#include <cassert>

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
    m_OnCompleteCallback = nullptr;
    m_DeferredCancelCallback = nullptr;
    // jthread does request_stop() + join() by itself
}

void AsyncTask::requestCancel(std::function<void()> deferredCallback) {
    if (m_Running) {
        m_DeferredCancelCallback = std::move(deferredCallback);
        m_Thread.request_stop();
    }
}

void AsyncTask::join() {
    assert(producedResults());

    m_Running = false;
    const bool wasCancelled = m_Thread.get_stop_token().stop_requested();
    if (m_Thread.joinable())
        m_Thread.join();

    if (m_OnCompleteCallback)
        m_OnCompleteCallback(std::move(m_ErrorOpt), wasCancelled);

    if (wasCancelled && m_DeferredCancelCallback)
        m_DeferredCancelCallback();
}
