#ifndef CPU_PROFILER_H
#define CPU_PROFILER_H

#include <easy/profiler.h>

namespace IRProfile {
class CPUProfiler {
  public:
    ~CPUProfiler();
    static CPUProfiler &instance();
    void setEnabled(bool enabled);
    bool isEnabled() const;
    void mainThread();
    void shutdown();
    // inline void profileFunction(unsigned int color) {
    //     EASY_FUNCTION(color);
    // }
    // inline void profileBlock(
    //     const std::string name,
    //     unsigned int color
    // )
    // {
    //     EASY_BLOCK(name.c_str(), color);
    // }
  private:
    CPUProfiler();
    bool m_enabled = true;
    bool m_shutdown = false;
};

} // namespace IRProfile

#endif /* CPU_PROFILER_H */
