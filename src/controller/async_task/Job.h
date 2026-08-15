#pragma once

#include <cassert>
#include "AsyncTask.h"

// owns a payload together with the background task that operates on it
template <typename Payload>
class Job {
public:
    using Routine = std::function<std::optional<std::string>(Payload& payload, const std::stop_token& st)>;
    using OnDone = std::function<void(std::optional<std::string> errorOpt, bool wasCancelled)>;

    Job() = default;
    ~Job() = default;

    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;
    Job(Job&&) noexcept = delete;
    Job& operator=(Job&&) noexcept = delete;

    bool isAlive() const { return m_Alive; }
    bool isRunning() const { return !!m_TaskPtr; }

    const Payload& data() const { assert(m_Alive); return m_Payload; }
    Payload& data() { assert(m_Alive); return m_Payload; }

    void start(Payload&& payload, Routine routine, OnDone onDone) {
        assert(!m_Alive && !m_TaskPtr);
        m_Payload = std::move(payload);
        m_Alive = true;

        m_TaskPtr = std::make_unique<AsyncTask>([this, r = std::move(routine)] (const std::stop_token& st) {
            return r(m_Payload, st);
        }, std::move(onDone));
    }

    void checkFinished() {
        if (!m_TaskPtr || !m_TaskPtr->producedResults())
            return;

        // a completion callback is free to call releaseTask()/clear()/start() on this job,
        // so hand the task over to a local first - it must not die inside its own call
        const auto keepAlive = std::move(m_TaskPtr);
        keepAlive->join();
    }

    void cancel(std::function<void()> deferred = nullptr) const {
        if (m_TaskPtr) {
            m_TaskPtr->requestCancel(std::move(deferred));
            return;
        }
        if (deferred)
            deferred();
    }

    void releaseTask() { m_TaskPtr.reset(); }

    void clear() {
        releaseTask();
        m_Payload = Payload{};
        m_Alive = false;
    }
private:
    bool m_Alive = false;
    Payload m_Payload;
    std::unique_ptr<AsyncTask> m_TaskPtr;
};
