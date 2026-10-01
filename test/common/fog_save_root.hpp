#ifndef IR_TEST_FOG_SAVE_ROOT_H
#define IR_TEST_FOG_SAVE_ROOT_H

// PURPOSE: A fresh fog persistence root under the temp directory, removed on
//   scope exit, for tests that save a world field and reopen it.

#include <irreden/render/fog_world_field.hpp>
#include <irreden/world/field_chunk_persistence.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace IRTest {

class ScopedFogSaveRoot {
  public:
    ScopedFogSaveRoot() {
        static std::atomic<std::uint64_t> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path =
            std::filesystem::temp_directory_path() / ("ir-fog-save-root-" + std::to_string(stamp) +
                                                      "-" + std::to_string(counter.fetch_add(1)));
        std::filesystem::create_directories(m_path);
    }
    ~ScopedFogSaveRoot() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }
    ScopedFogSaveRoot(const ScopedFogSaveRoot &) = delete;
    ScopedFogSaveRoot &operator=(const ScopedFogSaveRoot &) = delete;

    /// Fog-layer persistence rooted at @p subdirectory of this root.
    IRWorld::FieldChunkDiskPersistence store(const std::string &subdirectory = "") const {
        return *IRWorld::FieldChunkDiskPersistence::create(
            (m_path / subdirectory).string(),
            IRPrefab::Fog::kFogFieldLayer,
            IRPrefab::Fog::kFogFieldBytesPerCell
        );
    }

  private:
    std::filesystem::path m_path;
};

} // namespace IRTest

#endif /* IR_TEST_FOG_SAVE_ROOT_H */
