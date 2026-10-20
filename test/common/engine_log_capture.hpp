#ifndef IR_TEST_ENGINE_LOG_CAPTURE_H
#define IR_TEST_ENGINE_LOG_CAPTURE_H

// PURPOSE: Scoped capture of EngineLog output, so a test can assert on what
//   the engine logged (and at which level) during one scope.

#include <irreden/profile/logger_spd.hpp>

#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>

namespace IRTest {

/// The sink is erased in the destructor: the logger is a process-global, so a
/// sink left attached would write into a dead stream on the next log line.
class EngineLogCapture {
  public:
    EngineLogCapture()
        : m_logger{LoggerSpd::instance()->getEngineLogger()}
        , m_sink{std::make_shared<spdlog::sinks::ostream_sink_st>(m_text)} {
        m_logger->sinks().push_back(m_sink);
    }

    ~EngineLogCapture() {
        auto &sinks = m_logger->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), m_sink), sinks.end());
    }

    EngineLogCapture(const EngineLogCapture &) = delete;
    EngineLogCapture &operator=(const EngineLogCapture &) = delete;

    std::string text() {
        m_logger->flush();
        return m_text.str();
    }

  private:
    std::ostringstream m_text;
    spdlog::logger *m_logger;
    std::shared_ptr<spdlog::sinks::sink> m_sink;
};

} // namespace IRTest

#endif /* IR_TEST_ENGINE_LOG_CAPTURE_H */
