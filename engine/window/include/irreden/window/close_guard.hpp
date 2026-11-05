#ifndef IR_WINDOW_CLOSE_GUARD_H
#define IR_WINDOW_CLOSE_GUARD_H

#include <functional>
#include <utility>

namespace IRWindow {

// The owner must keep captured state alive until the guard is removed or the
// window is destroyed. A veto consumes the request; confirmation requests again.
class CloseGuard {
  public:
    void set(std::function<bool()> guard) {
        m_guard = std::move(guard);
    }

    bool accepts(bool requested) const {
        return requested && (!m_guard || m_guard());
    }

  private:
    std::function<bool()> m_guard;
};

} // namespace IRWindow

#endif
