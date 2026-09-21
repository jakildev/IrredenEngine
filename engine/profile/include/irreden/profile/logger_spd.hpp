// Logger for the irreden engine.
// Uses spdlogger: https://github.com/gabime/spdlog

#ifndef LOGGER_SPD_H
#define LOGGER_SPD_H

#include <spdlog/spdlog.h>

#include <fstream>
#include <memory>

class LoggerSpd {
  public:
    static LoggerSpd *instance();
    inline spdlog::logger *getEngineLogger() {
        return m_engineLogger.get();
    }
    inline spdlog::logger *getGLAPILogger() {
        return m_GLAPILogger.get();
    }
    inline spdlog::logger *getGameLogger() {
        return m_clientLogger.get();
    }
    // Sink for Lua `print`; see engine/script/CLAUDE.md for the contract.
    inline spdlog::logger *getScriptLogger() {
        return m_scriptLogger.get();
    }

  private:
    LoggerSpd();
    // shared_ptr is required by spdlog::register_logger.
    std::shared_ptr<spdlog::logger> m_engineLogger;
    std::shared_ptr<spdlog::logger> m_GLAPILogger;
    std::shared_ptr<spdlog::logger> m_clientLogger;
    std::shared_ptr<spdlog::logger> m_scriptLogger;
};

#endif /* LOGGER_SPD_H */
