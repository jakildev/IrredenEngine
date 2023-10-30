
#include <irreden/profile/logger_spd.hpp>
#include <irreden/profile/cpu_profiler.hpp>

#include <cstdarg>
namespace IRProfile {

    // TODO: Disable logging and asserts in release mode

    template <typename... Args>
    inline void engAssert(bool condition, const char* format, Args&&... args) {
        if (!condition) {
            LoggerSpd::instance()->getEngineLogger()->critical(fmt::runtime(format), args...);
            throw std::runtime_error("Engine assertion failed");
        }
    }

    template <typename... Args>
    inline void glAssert(bool condition, const char* format, Args&&... args) {
        if (!condition) {
            LoggerSpd::instance()->getGLAPILogger()->critical(fmt::runtime(format), args...);
            throw std::runtime_error("GL assertion failed");
        }
    }

    // Game logging API

    template <typename... Args>
    inline void logTrace(const char* format, Args&&... args) {
        LoggerSpd::instance()->getGameLogger()->trace(fmt::runtime(format), args...);
    }
    template <typename... Args>
    inline void logDebug(const char* format, Args&&... args) {
        LoggerSpd::instance()->getGameLogger()->debug(fmt::runtime(format), args...);
    }
    template <typename... Args>
    inline void logInfo(const char* format, Args&&... args) {
        LoggerSpd::instance()->getGameLogger()->info(fmt::runtime(format), args...);
    }
    template <typename... Args>
    inline void logWarn(const char* format, Args&&... args) {
        LoggerSpd::instance()->getGameLogger()->warn(fmt::runtime(format), args...);
    }
    template <typename... Args>
    inline void logError(const char* format, Args&&... args) {
        LoggerSpd::instance()->getGameLogger()->error(fmt::runtime(format), args...);
    }
    template <typename... Args>
    inline void logFatal(const char* format, Args&&... args) {
        LoggerSpd::instance()->getGameLogger()->critical(fmt::runtime(format), args...);
    }

    // Engine logging API

    template <typename... Args>
    inline void engLogTrace(const char* format, Args&&... args) {
        LoggerSpd::instance()->getEngineLogger()->trace(fmt::runtime(format), args...);
    }
    template <typename... Args>
    inline void engLogDebug(const char* format, Args&&... args) {
        LoggerSpd::instance()->getEngineLogger()->debug(fmt::runtime(format), args...);
    }
    template <typename... Args>
    inline void engLogInfo(const char* format, Args&&... args) {
        LoggerSpd::instance()->getEngineLogger()->info(fmt::runtime(format), args...);
    }
    template <typename... Args>
    inline void engLogWarn(const char* format, Args&&... args) {
        LoggerSpd::instance()->getEngineLogger()->warn(fmt::runtime(format), args...);
    }
    template <typename... Args>
    inline void engLogError(const char* format, Args&&... args) {
        LoggerSpd::instance()->getEngineLogger()->error(fmt::runtime(format), args...);
    }
    template <typename... Args>
    inline void engLogFatal(const char* format, Args&&... args) {
        LoggerSpd::instance()->getEngineLogger()->critical(fmt::runtime(format), args...);
    }

    // GL logging API
    template <typename... Args>
    inline void glLogDebug(const char* format, Args&&... args) {
        LoggerSpd::instance()->getGLAPILogger()->debug(fmt::runtime(format), args...);
    }
    template <typename... Args>
    inline void glLogFatal(const char* format, Args&&... args) {
        LoggerSpd::instance()->getGLAPILogger()->critical(fmt::runtime(format), args...);
    }

} // namespace IRProfile