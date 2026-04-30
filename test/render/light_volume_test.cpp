#include <gtest/gtest.h>

#include <irreden/render/systems/system_compute_light_volume.hpp>

namespace {

std::vector<std::uint8_t> makeLightBuffer() {
    return std::vector<std::uint8_t>(
        static_cast<std::size_t>(IRComponents::kLightVolumeSize) *
            static_cast<std::size_t>(IRComponents::kLightVolumeSize) *
            static_cast<std::size_t>(IRComponents::kLightVolumeSize) * 4u,
        0u
    );
}

std::uint8_t redAt(const std::vector<std::uint8_t> &buffer, int x, int y, int z) {
    const std::size_t index = IRComponents::C_CanvasLightVolume::flatIndex(x, y, z) * 4u;
    return buffer[index];
}

TEST(PointLightVolume, OccluderBlocksCellsBehindIt) {
    IRComponents::C_OccupancyGrid grid{128};
    grid.setBit(5, 0, 0);
    auto buffer = makeLightBuffer();

    IRSystem::detail::fillPointLight(
        grid,
        buffer,
        IRMath::ivec3(0, 0, 0),
        IRMath::Color{255, 0, 0, 255},
        1.0f,
        10
    );

    EXPECT_GT(redAt(buffer, 2, 0, 0), 0);
    EXPECT_EQ(redAt(buffer, 8, 0, 0), 0);
}

TEST(PointLightVolume, UsesEuclideanSphereFalloff) {
    IRComponents::C_OccupancyGrid grid{128};
    auto buffer = makeLightBuffer();

    IRSystem::detail::fillPointLight(
        grid,
        buffer,
        IRMath::ivec3(0, 0, 0),
        IRMath::Color{255, 0, 0, 255},
        1.0f,
        10
    );

    EXPECT_GT(redAt(buffer, 3, 4, 0), 0);
    EXPECT_EQ(redAt(buffer, 8, 8, 0), 0);
}

TEST(SpotLightVolume, ConeRejectsCellsOutsideDirection) {
    IRComponents::C_OccupancyGrid grid{128};
    auto buffer = makeLightBuffer();

    IRSystem::detail::fillSpotLight(
        grid,
        buffer,
        IRMath::ivec3(0, 0, 0),
        IRMath::Color{255, 0, 0, 255},
        1.0f,
        10,
        IRMath::vec3(0.0f, 1.0f, 0.0f),
        30.0f
    );

    EXPECT_GT(redAt(buffer, 0, 5, 0), 0);
    EXPECT_EQ(redAt(buffer, 5, 0, 0), 0);
}

TEST(SpotLightVolume, OccluderBlocksCellsInsideCone) {
    IRComponents::C_OccupancyGrid grid{128};
    grid.setBit(0, 5, 0);
    auto buffer = makeLightBuffer();

    IRSystem::detail::fillSpotLight(
        grid,
        buffer,
        IRMath::ivec3(0, 0, 0),
        IRMath::Color{255, 0, 0, 255},
        1.0f,
        10,
        IRMath::vec3(0.0f, 1.0f, 0.0f),
        45.0f
    );

    EXPECT_GT(redAt(buffer, 0, 4, 0), 0);
    EXPECT_EQ(redAt(buffer, 0, 8, 0), 0);
}

} // namespace
