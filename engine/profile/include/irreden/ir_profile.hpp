#ifndef IR_PROFILE_H
#define IR_PROFILE_H

// This is the API for the profiling module
#include <irreden/profile/logger_spd.hpp>
#include <irreden/profile/cpu_profiler.hpp>

#include <string>
#include <cstdarg>
#include <vector>

namespace IRProfile {

// Profiler colors
// TODO: make these unsigned ints
#define IR_PROFILER_COLOR_UPDATE 0xff0000ff
#define IR_PROFILER_COLOR_RENDER 0xffff0000
#define IR_PROFILER_COLOR_AUDIO 0xff00ffff
#define IR_PROFILER_COLOR_ENTITY_OPS 0xfff0ff00
#define IR_PROFILER_COLOR_SYSTEMS 0xff0f0f00
#define IR_PROFILER_COLOR_COMMANDS 0x88Ff0f00
#define IR_PROFILER_COLOR_BEGIN_EXECUTE 0xff00ff00
#define IR_PROFILER_COLOR_END_EXECUTE 0xff00ff00
#define IR_PROFILER_COLOR_INPUT 0xff4444ff
#define IR_PROFILER_COLOR_VIDEO 0xffff8800

template <typename... Args>
inline void engAssert(
    bool condition,
    const char *filepath,
    const char *functionName,
    int lineNumber,
    const char *assertionString,
    const char *format,
    Args &&...args
);

// Game logging commands
template <typename... Args> inline void logTrace(const char *format, Args &&...args);
template <typename... Args> inline void logDebug(const char *format, Args &&...args);
template <typename... Args> inline void logInfo(const char *format, Args &&...args);
template <typename... Args> inline void logWarn(const char *format, Args &&...args);
template <typename... Args> inline void logError(const char *format, Args &&...args);
template <typename... Args> inline void logFatal(const char *format, Args &&...args);

// Engine logging commands
template <typename... Args> inline void engLogTrace(const char *format, Args &&...args);
template <typename... Args> inline void engLogDebug(const char *format, Args &&...args);
template <typename... Args> inline void engLogInfo(const char *format, Args &&...args);
template <typename... Args> inline void engLogWarn(const char *format, Args &&...args);
template <typename... Args> inline void engLogError(const char *format, Args &&...args);
template <typename... Args> inline void engLogFatal(const char *format, Args &&...args);

// GL logging commands
template <typename... Args> inline void glLogDebug(const char *format, Args &&...args);
template <typename... Args> inline void glLogFatal(const char *format, Args &&...args);

} // namespace IRProfile

#include <irreden/profile/ir_profile.tpp>

// TODO: somewhere else
// #define IR_RELEASE

#ifndef IR_RELEASE

#define IR_ASSERT(x, en, ...)                                                                      \
    IRProfile::engAssert(x, __FILE__, __FUNCTION__, __LINE__, #x, en, ##__VA_ARGS__)
#define IR_PROFILE_FUNCTION(color) EASY_FUNCTION(color)
#define IR_PROFILE_BLOCK(name, color) EASY_BLOCK(name, color)
#define IR_PROFILE_END_BLOCK EASY_END_BLOCK
#define IR_PROFILE_MAIN_THREAD EASY_MAIN_THREAD

#define IR_LOG_TRACE(...) IRProfile::logTrace(__VA_ARGS__)
#define IR_LOG_DEBUG(...) IRProfile::logDebug(__VA_ARGS__)
#define IR_LOG_INFO(...) IRProfile::logInfo(__VA_ARGS__)
#define IR_LOG_WARN(...) IRProfile::logWarn(__VA_ARGS__)
#define IR_LOG_ERROR(...) IRProfile::logError(__VA_ARGS__)
#define IR_LOG_FATAL(...) IRProfile::logFatal(__VA_ARGS__)

#define IRE_LOG_TRACE(...) IRProfile::engLogTrace(__VA_ARGS__)
#define IRE_LOG_DEBUG(...) IRProfile::engLogDebug(__VA_ARGS__)
#define IRE_LOG_INFO(...) IRProfile::engLogInfo(__VA_ARGS__)
#define IRE_LOG_WARN(...) IRProfile::engLogWarn(__VA_ARGS__)
#define IRE_LOG_ERROR(...) IRProfile::engLogError(__VA_ARGS__)
#define IRE_LOG_FATAL(...) IRProfile::engLogFatal(__VA_ARGS__)

#define IRE_GL_LOG_DEBUG(...) IRProfile::glLogDebug(__VA_ARGS__)
#define IRE_GL_LOG_FATAL(...) IRProfile::glLogFatal(__VA_ARGS__)

#else

#define IR_ASSERT(x, en, ...)
#define IR_PROFILE_FUNCTION(color)
#define IR_PROFILE_BLOCK(name, color)
#define IR_PROFILE_END_BLOCK
#define IR_PROFILE_MAIN_THREAD

#define IR_LOG_TRACE(...)
#define IR_LOG_DEBUG(...)
#define IR_LOG_INFO(...)
#define IR_LOG_WARN(...)
#define IR_LOG_ERROR(...)
#define IR_LOG_FATAL(...)

#define IRE_LOG_TRACE(...)
#define IRE_LOG_DEBUG(...)
#define IRE_LOG_INFO(...)
#define IRE_LOG_WARN(...)
#define IRE_LOG_ERROR(...)
#define IRE_LOG_FATAL(...)

#define IRE_GL_LOG_DEBUG(...)
#define IRE_GL_LOG_FATAL(...)

#endif // IR_RELEASE

#endif /* IR_PROFILE_H */
