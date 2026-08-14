#pragma once

#include "AsyncTask.h"
#include "../../ui/Ui.h"

// owns a payload together with the background task that operates on it
template <typename Payload>
class Job {
public:
    using Routine = std::function<std::optional<std::string>(const Payload& payload, const std::stop_token& st, std::atomic<float>& progress)>;
    using OnDone = std::function<void(std::optional<std::string> errorOpt, bool wasCancelled)>;

    Job() = default;
    ~Job() = default; // TODO: should we call m_Alive = false???

    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;
    Job(Job&&) noexcept = delete;
    Job& operator=(Job&&) noexcept = delete;

    bool isAlive() const { return m_Alive; }
    bool isRunning() const { return !!m_TaskPtr; }

    const Payload& data() const { assert(m_Alive); return m_Payload; }
    Payload& data() { assert(m_Alive); return m_Payload; }
    Ui::ProgressRAII* progressPtr() const { return m_ProgressPtr.get(); }

    void start(Payload&& payload, std::unique_ptr<Ui::ProgressRAII> progressPtr, Routine routine, OnDone onDone) {
        assert(!m_Alive && !m_TaskPtr);
        m_Payload = std::move(payload);
        m_ProgressPtr = std::move(progressPtr);
        m_Alive = true;

        auto& progressRef = m_ProgressPtr->getProgressTS();
        m_TaskPtr = std::make_unique<AsyncTask>([this, r = std::move(routine), &progressRef] (const std::stop_token& st) {
            return r(m_Payload, st, progressRef);
        }, std::move(onDone));
    }

    void checkFinished() const {
        if (m_TaskPtr)
            m_TaskPtr->checkFinished();
    }

    // marks the task cancelled and invokes `deferred` once the worker has actually stopped
    // if nothing is running, `deferred` runs immediately
    void cancel(std::function<void()> deferred = nullptr) const {
        if (m_TaskPtr) {
            if (m_ProgressPtr)
                m_ProgressPtr->setCancelled();
            m_TaskPtr->requestCancel(std::move(deferred));
            return;
        }
        if (deferred)
            deferred();
    }

    void releaseTask() {
        m_TaskPtr.reset();
        m_ProgressPtr.reset();
    }

    void clear() {
        releaseTask();
        m_Payload = Payload{};
        m_Alive = false;
    }
private:
    bool m_Alive = false;
    Payload m_Payload;
    std::unique_ptr<Ui::ProgressRAII> m_ProgressPtr;
    std::unique_ptr<AsyncTask> m_TaskPtr;
};
