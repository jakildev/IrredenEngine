#ifndef IR_TEST_ALLOCATION_COUNTER_H
#define IR_TEST_ALLOCATION_COUNTER_H

// PURPOSE: The witness for an allocation-free steady state. Counts the global
//   `operator new` calls the calling thread makes after construction;
//   allocation_counter.cpp replaces the global operator new/delete for the
//   whole IrredenEngineTest binary to feed it.

#include <cstddef>

namespace IRTest {

class AllocationCounter {
  public:
    AllocationCounter();

    /// `operator new` calls on this thread since construction.
    std::size_t allocations() const;

  private:
    std::size_t m_start;
};

} // namespace IRTest

#endif /* IR_TEST_ALLOCATION_COUNTER_H */
