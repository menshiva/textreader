#pragma once

#include <chrono>

class ScopedProfiler {
public:
    explicit ScopedProfiler(const char* name) : m_Name(name), m_Start(std::chrono::steady_clock::now()) {}

    ~ScopedProfiler() { report(); }

    void report() const {
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_Start).count();
        printf("[%s] %.1f ms\n", m_Name, ms);
    }
private:
    const char* m_Name;
    std::chrono::steady_clock::time_point m_Start;
};
