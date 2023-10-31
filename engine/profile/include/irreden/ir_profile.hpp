/*
 * Project: Irreden Engine
 * File: ir_profiling.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_PROFILING_H
#define IR_PROFILING_H

// This is the API for the profiling module
#include <string>
#include <cstdarg>
#include <vector>
#include <irreden/profile/logger_spd.hpp>

namespace IRProfile {

    // Profiler colors
    // TODO: make these unsigned ints
    #define IR_PROFILER_COLOR_UPDATE     0xff0000ff
    #define IR_PROFILER_COLOR_RENDER     0xffff0000
    #define IR_PROFILER_COLOR_AUDIO      0xff00ffff
    #define IR_PROFILER_COLOR_ENTITY_OPS 0xfff0ff00
    #define IR_PROFILER_COLOR_SYSTEMS    0xff0f0f00
    #define IR_PROFILER_COLOR_COMMANDS    0x88Ff0f00
    #define IR_PROFILER_COLOR_BEGIN_EXECUTE      0xff00ff00
    #define IR_PROFILER_COLOR_END_EXECUTE        0xff00ff00
    #define IR_PROFILER_COLOR_INPUT      0xff4444ff

    template <typename... Args>
    inline void engAssert(
        bool condition,
        const char* filepath,
        const char* functionName,
        int lineNumber,
        const char* assertionString,
        const char* format,
        Args&&... args
    );

    // Game logging commands
    template <typename... Args>
    inline void logTrace(const char* format, Args&&... args);
    template <typename... Args>
    inline void logDebug(const char* format, Args&&... args);
    template <typename... Args>
    inline void logInfo(const char* format, Args&&... args);
    template <typename... Args>
    inline void logWarn(const char* format, Args&&... args);
    template <typename... Args>
    inline void logError(const char* format, Args&&... args);
    template <typename... Args>
    inline void logFatal(const char* format, Args&&... args);

    // Engine logging commands
    template <typename... Args>
    inline void engLogTrace(const char* format, Args&&... args);
    template <typename... Args>
    inline void engLogDebug(const char* format, Args&&... args);
    template <typename... Args>
    inline void engLogInfo(const char* format, Args&&... args);
    template <typename... Args>
    inline void engLogWarn(const char* format, Args&&... args);
    template <typename... Args>
    inline void engLogError(const char* format, Args&&... args);
    template <typename... Args>
    inline void engLogFatal(const char* format, Args&&... args);

    // GL logging commands
    template <typename... Args>
    inline void glLogDebug(const char* format, Args&&... args);
    template <typename... Args>
    inline void glLogFatal(const char* format, Args&&... args);

    // CPU profiling commands
    void profileMainThread();
    void profileFunction(unsigned int color);
    void profileBlock(const std::string name, unsigned int color);

} // namespace IRProfile

#include <irreden/profile/ir_profile.tpp>

#endif /* IR_PROFILING_H */
