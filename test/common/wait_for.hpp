#ifndef IR_TEST_WAIT_FOR_H
#define IR_TEST_WAIT_FOR_H

// PURPOSE: Bounded poll for a condition another thread brings about, so a
//   test that waits on a worker fails at the limit instead of hanging.

#include <chrono>
#include <thread>

namespace IRTest {

/// Polls @p reached once a millisecond until it holds; false if @p limit
/// passes first.
template <typename Predicate>
bool waitFor(Predicate reached, std::chrono::milliseconds limit = std::chrono::seconds(5)) {
    const auto giveUpAt = std::chrono::steady_clock::now() + limit;
    while (!reached()) {
        if (std::chrono::steady_clock::now() > giveUpAt) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

} // namespace IRTest

#endif /* IR_TEST_WAIT_FOR_H */
