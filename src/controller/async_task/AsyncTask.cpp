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
    // jthread does request_stop() + join() by itself
}

void AsyncTask::requestCancel() {
    if (m_Running)
        m_Thread.request_stop();
}

void AsyncTask::complete() {
    assert(isFinished());

    m_Running = false;
    const bool wasCancelled = m_Thread.get_stop_token().stop_requested();
    if (m_Thread.joinable())
        m_Thread.join();

    if (m_OnCompleteCallback)
        m_OnCompleteCallback(std::move(m_ErrorOpt), wasCancelled);
}
