#include <gtest/gtest.h>

#include <irreden/ir_render.hpp>

#include <stdexcept>

TEST(SunDirectionConvention, PositiveZIsRejected) {
    EXPECT_THROW(IRRender::setSunDirection(vec3(0.0f, 0.0f, 1.0f)), std::runtime_error);
}
