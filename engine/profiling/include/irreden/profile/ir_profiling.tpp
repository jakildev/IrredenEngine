
#include <irreden/profile/logger_spd.hpp>
#include <irreden/profile/cpu_profiler.hpp>

#include <cstdarg>
namespace IRProfile {

    // TODO: Disable logging and asserts in release mode

    inline std::vector<char> formattedString(const char* format, va_list args) {
        va_list argsCopy;
        va_copy(argsCopy, args);
        int size = _vscprintf(format, argsCopy) + 1; // +1 for null-terminator
        std::vector<char> message(size);
        vsnprintf(message.data(), size, format, args);
        va_end(argsCopy);
        return message;
    }

    inline void engAssert(bool condition, const char* format, ...) {
        if (!condition) {
            va_list args;
            auto message = formattedString(format, args);
            LoggerSpd::instance()->getEngineLogger()->critical("{}", message.data());
            throw std::runtime_error("Engine assertion failed");
        }
    }
    inline void glAssert(bool condition, const char* format, ...) {
        if (!condition) {
            va_list args;
            auto message = formattedString(format, args);
            LoggerSpd::instance()->getGLAPILogger()->critical("{}", message.data());

            throw std::runtime_error("GL assertion failed");
        }
    }

    // Game logging API

    inline void logTrace(const char* format, ...) {
        va_list args;
        auto message = formattedString(format, args);
        LoggerSpd::instance()->getGameLogger()->trace("{}", message.data());
    }
    inline void logDebug(const char* format, ...) {
        va_list args;
        auto message = formattedString(format, args);
        LoggerSpd::instance()->getGameLogger()->debug("{}", message.data());
    }
    inline void logInfo(const char* format, ...) {
        va_list args;
        auto message = formattedString(format, args);
        LoggerSpd::instance()->getGameLogger()->info("{}", message.data());
    }
    inline void logWarn(const char* format, ...) {
        va_list args;
        auto message = formattedString(format, args);
        LoggerSpd::instance()->getGameLogger()->warn("{}", message.data());
    }
    inline void logError(const char* format, ...) {
        va_list args;
        auto message = formattedString(format, args);
        LoggerSpd::instance()->getGameLogger()->error("{}", message.data());
    }
    inline void logFatal(const char* format, ...) {
        va_list args;
        auto message = formattedString(format, args);
        LoggerSpd::instance()->getGameLogger()->critical("{}", message.data());
    }

    // Engine logging API

    inline void engLogTrace(const char* format, ...) {
        va_list args;
        auto message = formattedString(format, args);
        LoggerSpd::instance()->getEngineLogger()->trace("{}", message.data());
    }
    inline void engLogDebug(const char* format, ...) {
        va_list args;
        auto message = formattedString(format, args);
        LoggerSpd::instance()->getEngineLogger()->debug("{}", message.data());
    }
    inline void engLogInfo(const char* format, ...) {
        va_list args;
        auto message = formattedString(format, args);
        LoggerSpd::instance()->getEngineLogger()->info("{}", message.data());
    }
    inline void engLogWarn(const char* format, ...) {
        va_list args;
        auto message = formattedString(format, args);
        LoggerSpd::instance()->getEngineLogger()->warn("{}", message.data());
    }
    inline void engLogError(const char* format, ...) {
        va_list args;
        auto message = formattedString(format, args);
        LoggerSpd::instance()->getEngineLogger()->error("{}", message.data());
    }
    inline void engLogFatal(const char* format, ...) {
        va_list args;
        auto message = formattedString(format, args);
        LoggerSpd::instance()->getEngineLogger()->critical("{}", message.data());
    }

    // GL logging API
    inline void glLogDebug(const char* format, ...) {
        va_list args;
        auto message = formattedString(format, args);
        LoggerSpd::instance()->getGLAPILogger()->debug("{}", message.data());
    }
    inline void glLogFatal(const char* format, ...) {
        va_list args;
        auto message = formattedString(format, args);
        LoggerSpd::instance()->getGLAPILogger()->critical("{}", message.data());
    }

} // namespace IRProfile