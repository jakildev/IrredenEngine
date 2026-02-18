
#include <irreden/profile/logger_spd.hpp>
#include <irreden/profile/cpu_profiler.hpp>

#include <string>
#include <utility>

namespace IRProfile {

    // TODO: Disable logging and asserts in release mode
    namespace {
        template <typename... Args>
        inline std::string formatRuntimeString(const char *format, Args &&...args) {
            return fmt::format(fmt::runtime(format), std::forward<Args>(args)...);
        }
    } // namespace

    template <typename... Args>
    inline void engAssert(
        bool condition,
        const char* filepath,
        const char* functionName,
        int lineNumber,
        const char* assertionString,
        const char* format,
        Args&&... args
    )
    {
        if (!condition) {
            const std::string errorMessage = formatRuntimeString(format, std::forward<Args>(args)...);
            LoggerSpd::instance()->getGLAPILogger()->critical(
                "ASSERTION: {}\n\tFILE: {}\n\tFUNCTION: {}(...)\n\tLINE: {}\n\tERROR: {}",
                assertionString,
                filepath,
                functionName,
                lineNumber,
                errorMessage
            );
            throw std::runtime_error("Engine assertion failed");
        }
    }

    // Game logging API

    template <typename... Args>
    inline void logTrace(const char* format, Args&&... args) {
        const std::string message = formatRuntimeString(format, std::forward<Args>(args)...);
        LoggerSpd::instance()->getGameLogger()->trace(
            "{}", message
        );
    }
    template <typename... Args>
    inline void logDebug(const char* format, Args&&... args) {
        const std::string message = formatRuntimeString(format, std::forward<Args>(args)...);
        LoggerSpd::instance()->getGameLogger()->debug(
            "{}", message
        );
    }
    template <typename... Args>
    inline void logInfo(const char* format, Args&&... args) {
        const std::string message = formatRuntimeString(format, std::forward<Args>(args)...);
        LoggerSpd::instance()->getGameLogger()->info(
            "{}", message
        );
    }
    template <typename... Args>
    inline void logWarn(const char* format, Args&&... args) {
        const std::string message = formatRuntimeString(format, std::forward<Args>(args)...);
        LoggerSpd::instance()->getGameLogger()->warn(
            "{}", message
        );
    }
    template <typename... Args>
    inline void logError(const char* format, Args&&... args) {
        const std::string message = formatRuntimeString(format, std::forward<Args>(args)...);
        LoggerSpd::instance()->getGameLogger()->error(
            "{}", message
        );
    }
    template <typename... Args>
    inline void logFatal(const char* format, Args&&... args) {
        const std::string message = formatRuntimeString(format, std::forward<Args>(args)...);
        LoggerSpd::instance()->getGameLogger()->critical(
            "{}", message
        );
    }

    // Engine logging API

    template <typename... Args>
    inline void engLogTrace(const char* format, Args&&... args) {
        const std::string message = formatRuntimeString(format, std::forward<Args>(args)...);
        LoggerSpd::instance()->getEngineLogger()->trace(
            "{}", message
        );
    }
    template <typename... Args>
    inline void engLogDebug(const char* format, Args&&... args) {
        const std::string message = formatRuntimeString(format, std::forward<Args>(args)...);
        LoggerSpd::instance()->getEngineLogger()->debug(
            "{}", message
        );
    }
    template <typename... Args>
    inline void engLogInfo(const char* format, Args&&... args) {
        const std::string message = formatRuntimeString(format, std::forward<Args>(args)...);
        LoggerSpd::instance()->getEngineLogger()->info(
            "{}", message
        );
    }
    template <typename... Args>
    inline void engLogWarn(const char* format, Args&&... args) {
        const std::string message = formatRuntimeString(format, std::forward<Args>(args)...);
        LoggerSpd::instance()->getEngineLogger()->warn(
            "{}", message
        );
    }
    template <typename... Args>
    inline void engLogError(const char* format, Args&&... args) {
        const std::string message = formatRuntimeString(format, std::forward<Args>(args)...);
        LoggerSpd::instance()->getEngineLogger()->error(
            "{}", message
        );
    }
    template <typename... Args>
    inline void engLogFatal(const char* format, Args&&... args) {
        const std::string message = formatRuntimeString(format, std::forward<Args>(args)...);
        LoggerSpd::instance()->getEngineLogger()->critical(
            "{}", message
        );
    }

    // GL logging API
    template <typename... Args>
    inline void glLogDebug(const char* format, Args&&... args) {
        const std::string message = formatRuntimeString(format, std::forward<Args>(args)...);
        LoggerSpd::instance()->getGLAPILogger()->debug(
            "{}", message
        );
    }
    template <typename... Args>
    inline void glLogFatal(const char* format, Args&&... args) {
        const std::string message = formatRuntimeString(format, std::forward<Args>(args)...);
        LoggerSpd::instance()->getGLAPILogger()->critical(
            "{}", message
        );
    }

} // namespace IRProfile
