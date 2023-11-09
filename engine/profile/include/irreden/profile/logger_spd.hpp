/*
 * Project: Irreden Engine
 * File: logger_spd.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Logger for the irreden engine.
// Uses spdlogger: https://github.com/gabime/spdlog

#ifndef LOGGER_SPD_H
#define LOGGER_SPD_H

#include <spdlog/spdlog.h>

#include <fstream>
#include <memory>

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

#endif /* LOGGER_SPD_H */
