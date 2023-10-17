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
#define ENG_LOG_DEBUG(...)  LoggerSpd::instance()->getEngineLogger()->debug(__VA_ARGS__)
#define ENG_LOG_INFO(...)   LoggerSpd::instance()->getEngineLogger()->info(__VA_ARGS__)
#define ENG_LOG_ERROR(...)  LoggerSpd::instance()->getEngineLogger()->error(__VA_ARGS__)
#define ENG_LOG_WARN(...)   LoggerSpd::instance()->getEngineLogger()->warn(__VA_ARGS__)
#define ENG_LOG_FATAL(...)  LoggerSpd::instance()->getEngineLogger()->critical(__VA_ARGS__)
#define ENG_LOG_TRACE(...)  LoggerSpd::instance()->getEngineLogger()->trace(__VA_ARGS__)

// TODO: Move asserts somewhere else
#define ENG_ASSERT(x, m) if ((x)) {} else { ENG_LOG_FATAL("ASSERT: {}\n\t{}\n\tFILE: {}\n\tFUNCTION: {}\n\tLINE: {}", #x, m, __FILE__, __FUNCTION__, __LINE__); exit(EXIT_FAILURE); }

#define GLAPI_LOG_DEBUG(...)  LoggerSpd::instance()->getGLAPILogger()->debug(__VA_ARGS__)
#define GLAPI_LOG_FATAL(...)  LoggerSpd::instance()->getGLAPILogger()->critical(__VA_ARGS__)
#define GLAPI_ASSERT(x, en) if ((x)) {} else { GLAPI_LOG_FATAL("ASSERT: {}\n\tERROR: {}\n\tFILE: {}\n\tFUNCTION: {}\n\tLINE: {}", #x, en, __FILE__, __FUNCTION__, __LINE__); exit(EXIT_FAILURE); }

#define GAME_LOG_DEBUG(...) LoggerSpd::instance()->getGameLogger()->debug(__VA_ARGS__)
#define GAME_LOG_INFO(...)  LoggerSpd::instance()->getGameLogger()->info(__VA_ARGS__)
#define GAME_LOG_ERROR(...) LoggerSpd::instance()->getGameLogger()->error(__VA_ARGS__)
#define GAME_LOG_WARN(...)  LoggerSpd::instance()->getGameLogger()->warn(__VA_ARGS__)
#define GAME_LOG_FATAL(...) LoggerSpd::instance()->getGameLogger()->critical(__VA_ARGS__)
#define GAME_LOG_TRACE(...) LoggerSpd::instance()->getGameLogger()->trace(__VA_ARGS__)

#else
#define ENG_LOG_DEBUG(...)  (void)0
#define ENG_LOG_INFO(...)   (void)0
#define ENG_LOG_ERROR(...)  (void)0
#define ENG_LOG_WARN(...)   (void)0
#define ENG_LOG_FATAL(...)  (void)0
#define ENG_LOG_TRACE(...)  (void)0

#define ENG_ASSERT(x, m) ((void)0)
#define GLAPI_LOG_DEBUG(...) (void)0
#define GLAPI_LOG_FATAL(...) (void)0
#define GLAPI_ASSERT(x, en) ((void)0)

#define GAME_LOG_DEBUG(...) (void)0
#define GAME_LOG_INFO(...)  (void)0
#define GAME_LOG_ERROR(...) (void)0
#define GAME_LOG_WARN(...)  (void)0
#define GAME_LOG_FATAL(...) (void)0
#define GAME_LOG_TRACE(...) (void)0
#endif

#endif /* LOGGER_SPD_H */
