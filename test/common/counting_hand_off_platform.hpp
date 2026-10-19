#ifndef TEST_COUNTING_HAND_OFF_PLATFORM_H
#define TEST_COUNTING_HAND_OFF_PLATFORM_H

#include <irreden/world/hand_off.hpp>

#include <string>
#include <vector>

namespace IRTest {

class CountingHandOffPlatform : public IREngine::HandOffPlatform {
  public:
    void drainGpu() override {
        ++drains_;
    }

    IREngine::ChildExit runToExit(const std::vector<std::string> &) override {
        ++launches_;
        return {true, 0, ""};
    }

    int drains_ = 0;
    int launches_ = 0;
};

} // namespace IRTest

#endif /* TEST_COUNTING_HAND_OFF_PLATFORM_H */
