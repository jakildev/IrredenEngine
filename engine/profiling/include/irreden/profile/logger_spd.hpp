/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\profiling\logger_spd.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef LOGGER_SPD_H
#define LOGGER_SPD_H

#include <fstream>
#include <memory>
#include <spdlog/spdlog.h>

// TODO: Not use pointers and singleton for logger and just use
// class members.

// A wrapper for the spdlogger: https://github.com/gabime/spdlog

class LoggerSpd {
public:
    static LoggerSpd* instance();
    inline spdlog::logger* getEngineLogger() { return m_engineLogger.get(); }
    inline spdlog::logger* getGLAPILogger() { return m_GLAPILogger.get(); }
    inline spdlog::logger* getGameLogger() { return m_clientLogger.get(); }
private:
    LoggerSpd();
    // TODO: Make unique pointer
    std::shared_ptr<spdlog::logger> m_engineLogger;
    std::shared_ptr<spdlog::logger> m_GLAPILogger;
    std::shared_ptr<spdlog::logger> m_clientLogger;
};

#ifndef IRREDEN_RELEASE_BUILD

// TODO: Move asserts somewhere else
// #define IRProfile::engAssert(x, m) if ((x)) {} else { IRProfile::engLogFatal("ASSERT: {}\n\t{}\n\tFILE: {}\n\tFUNCTION: {}\n\tLINE: {}", #x, m, __FILE__, __FUNCTION__, __LINE__); exit(EXIT_FAILURE); }

// #define IRProfile::glLogDebug(...)  LoggerSpd::instance()->getGLAPILogger()->debug(__VA_ARGS__)
// #define GLAPI_LOG_FATAL(...)  LoggerSpd::instance()->getGLAPILogger()->critical(__VA_ARGS__)
// #define IRProfile::glAssert(x, en) if ((x)) {} else { GLAPI_LOG_FATAL("ASSERT: {}\n\tERROR: {}\n\tFILE: {}\n\tFUNCTION: {}\n\tLINE: {}", #x, en, __FILE__, __FUNCTION__, __LINE__); exit(EXIT_FAILURE); }

#else
#define IRProfile::engLogDebug(...)  (void)0
#define IRProfile::engLogInfo(...)   (void)0
#define IRProfile::engLogError(...)  (void)0
#define IRProfile::engLogWarn(...)   (void)0
#define IRProfile::engLogFatal(...)  (void)0
#define ENG_LOG_TRACE(...)  (void)0

#define IRProfile::engAssert(x, m) ((void)0)
#define IRProfile::glLogDebug(...) (void)0
#define GLAPI_LOG_FATAL(...) (void)0
#define IRProfile::glAssert(x, en) ((void)0)

#define GAME_LOG_DEBUG(...) (void)0
#define GAME_LOG_INFO(...)  (void)0
#define GAME_LOG_ERROR(...) (void)0
#define GAME_LOG_WARN(...)  (void)0
#define GAME_LOG_FATAL(...) (void)0
#define GAME_LOG_TRACE(...) (void)0
#endif

#endif /* LOGGER_SPD_H */