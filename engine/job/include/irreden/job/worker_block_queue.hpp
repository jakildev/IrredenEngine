#ifndef WORKER_BLOCK_QUEUE_H
#define WORKER_BLOCK_QUEUE_H

#include <irreden/ir_job.hpp>

#include <atomic>
#include <cstddef>
#include <vector>

namespace IRJob {

/// Collects entries pushed from a `PARALLEL_FOR` tick for a serial drain
/// (typically `endTick`) without allocating on the tick path. `reset` sizes
/// the storage on the main thread for a population bound; each executor then
/// claims `kBlock` slots per atomic add, so the shared cursor is touched once
/// per block instead of once per entry. Every claimed block is full except
/// each executor's current one, whose unfilled tail `forEach` skips.
///
/// Push only from the thread whose `workerId()` names the slot (every
/// executor of one dispatch, main thread included); drain only after the
/// dispatch has joined.
template <typename Entry> class WorkerBlockQueue {
  public:
    static constexpr std::size_t kBlock = 64;

    /// Main thread, before the dispatch. `population` bounds the number of
    /// entries this dispatch can push. Storage only grows.
    void reset(std::size_t population) {
        const std::size_t executors = static_cast<std::size_t>(workerCount()) + 1u;
        const std::size_t capacity = population + executors * kBlock;
        if (m_entries.size() < capacity) {
            m_entries.resize(capacity);
        }
        m_cursors.assign(executors, Cursor{});
        m_claimed.store(0, std::memory_order_relaxed);
    }

    void push(const Entry &entry) {
        Cursor &cursor = m_cursors[static_cast<std::size_t>(workerId())];
        if (cursor.next_ == cursor.end_) {
            cursor.next_ = m_claimed.fetch_add(kBlock, std::memory_order_relaxed);
            cursor.end_ = cursor.next_ + kBlock;
        }
        m_entries[cursor.next_++] = entry;
    }

    /// Serial, after the dispatch has joined.
    template <typename Fn> void forEach(Fn &&fn) {
        const std::size_t claimed = m_claimed.load(std::memory_order_relaxed);
        for (std::size_t block = 0; block < claimed; block += kBlock) {
            std::size_t end = block + kBlock;
            for (const Cursor &cursor : m_cursors) {
                if (cursor.end_ == block + kBlock) {
                    end = cursor.next_;
                    break;
                }
            }
            for (std::size_t i = block; i < end; ++i) {
                fn(m_entries[i]);
            }
        }
    }

    std::size_t size() {
        std::size_t count = 0;
        forEach([&count](const Entry &) { ++count; });
        return count;
    }

  private:
    // One cache line per executor so a cursor bump never invalidates a
    // neighbour's.
    struct alignas(64) Cursor {
        std::size_t next_ = 0;
        std::size_t end_ = 0;
    };

    std::vector<Entry> m_entries;
    std::vector<Cursor> m_cursors;
    std::atomic<std::size_t> m_claimed{0};
};

} // namespace IRJob

#endif /* WORKER_BLOCK_QUEUE_H */
