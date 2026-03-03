#pragma once

#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

// Compile-time tracing:
// - Define `QTRADING_TRACE` for targets you want to instrument.
// - When not defined, QTR_TRACE compiles to nothing (zero overhead).

namespace QTrading::Debug {

#if defined(QTRADING_TRACE) && defined(QTRADING_TRACE_VERBOSE)

    inline std::mutex& trace_mtx() {
        static std::mutex m;
        return m;
    }

    inline uint64_t now_ms() {
        using namespace std::chrono;
        return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }

    inline void trace(const char* tag, const char* file, int line, const char* msg) {
        std::lock_guard<std::mutex> lk(trace_mtx());
        std::cerr << "[TRACE] t=" << now_ms()
                  << " tid=" << std::this_thread::get_id()
                  << " " << tag
                  << " " << file << ":" << line
                  << " " << msg << std::endl;
    }

    inline void trace(const char* tag, const char* file, int line, const std::string& msg) {
        std::lock_guard<std::mutex> lk(trace_mtx());
        std::cerr << "[TRACE] t=" << now_ms()
                  << " tid=" << std::this_thread::get_id()
                  << " " << tag
                  << " " << file << ":" << line
                  << " " << msg << std::endl;
    }

#   define QTR_TRACE(TAG, MSG) ::QTrading::Debug::trace((TAG), __FILE__, __LINE__, (MSG))
#else
#   define QTR_TRACE(TAG, MSG) do { (void)sizeof(TAG); } while(0)
#endif

} // namespace QTrading::Debug
