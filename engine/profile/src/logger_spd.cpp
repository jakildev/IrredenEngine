/*
 * Project: Irreden Engine
 * File: logger_spd.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/profile/logger_spd.hpp>

#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <iostream>
#include <ctime>

LoggerSpd::LoggerSpd() {
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    // consoleSink->
    // consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %^[%l] [%n] %v%$");

    /* add more sinks here */
    std::vector<spdlog::sink_ptr> sinks{ consoleSink};
    /* Logger for general game engine stuff */
    m_engineLogger = make_shared<spdlog::logger>("EngineLog", sinks.begin(), sinks.end());
    m_engineLogger->set_level(spdlog::level::info);
    m_engineLogger->flush_on(spdlog::level::trace);
    spdlog::register_logger(m_engineLogger);
    /* Logger for opengl api wrapper */
    // TODO: Have GLAPI logs go to file or somewhere else to not pollute terminal
    m_GLAPILogger = make_shared<spdlog::logger>("GLAPILog", sinks.begin(), sinks.end());
    m_GLAPILogger->set_level(spdlog::level::info); // Change to info to see all gl calls
    m_GLAPILogger->flush_on(spdlog::level::trace);
    spdlog::register_logger(m_GLAPILogger);
    /* Logger for game clients */
    m_clientLogger = make_shared<spdlog::logger>("ClientLog", sinks.begin(), sinks.end());
    m_clientLogger->set_level(spdlog::level::trace);
    m_clientLogger->flush_on(spdlog::level::trace);
    spdlog::register_logger(m_clientLogger);
}

LoggerSpd* LoggerSpd::instance() {
    /* cant use make_unique here because of private constructor */
    static std::unique_ptr<LoggerSpd> instance =
        std::unique_ptr<LoggerSpd>(new LoggerSpd{});
    return instance.get();
}

