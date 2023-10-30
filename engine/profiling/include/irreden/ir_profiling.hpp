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

    inline void engAssert(bool condition, const char* format, ...);
    inline void glAssert(bool condition, const char* format, ...);

    // Game logging commands
    inline void logTrace(const char* format, ...);
    inline void logDebug(const char* format, ...);
    inline void logInfo(const char* format, ...);
    inline void logWarn(const char* format, ...);
    inline void logError(const char* format, ...);
    inline void logFatal(const char* format, ...);

    // Engine logging commands
    inline void engLogTrace(const char* format, ...);
    inline void engLogDebug(const char* format, ...);
    inline void engLogInfo(const char* format, ...);
    inline void engLogWarn(const char* format, ...);
    inline void engLogError(const char* format, ...);
    inline void engLogFatal(const char* format, ...);

    // GL logging commands
    inline void glLogDebug(const char* format, ...);
    inline void glLogFatal(const char* format, ...);

    // CPU profiling commands
    void profileMainThread();
    void profileFunction(unsigned int color);
    void profileBlock(const std::string name, unsigned int color);

} // namespace IRProfile

#include <irreden/profile/ir_profiling.tpp>

#endif /* IR_PROFILING_H */
