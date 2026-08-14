#pragma once

#include <functional>
#include <optional>
#include <string>
#include <thread>

// runs a Work function on a background thread and reports the result back on the UI thread
class AsyncTask {
public:
    // called on a background thread, returns an error message
    using Work = std::function<std::optional<std::string>(const std::stop_token&)>;
    using OnComplete = std::function<void(std::optional<std::string> errorOpt, bool wasCancelled)>;

    AsyncTask(Work work, OnComplete onCompleteCallback);
    ~AsyncTask();

    AsyncTask(const AsyncTask&) = delete;
    AsyncTask& operator=(const AsyncTask&) = delete;
    AsyncTask(AsyncTask&&) noexcept = delete;
    AsyncTask& operator=(AsyncTask&&) noexcept = delete;

    void checkFinished();

    void requestCancel(std::function<void()> deferredCallback = nullptr);

    void unbindCallbacks();
private:
    bool m_Running = true;

    std::atomic<bool> m_Finished = false;
    std::optional<std::string> m_ErrorOpt;

    OnComplete m_OnCompleteCallback;
    std::function<void()> m_DeferredCancelCallback;

    std::jthread m_Thread;
};
