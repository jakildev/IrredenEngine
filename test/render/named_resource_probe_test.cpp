// The named-resource lookup pair: `getNamedResourceOrNull` probes and returns
// null on a miss, `getNamedResource` asserts and can never return null, and
// neither registers the name it failed to find. The asserting half is what
// makes a null check on its result dead code, so a caller whose contract is
// "no-op when the resource is absent" has to take the probing half (see
// #2627).
//
// GL-free by construction: `RenderingResourceManager`'s constructor only fills
// the id pool and registers the resource types, and a name miss returns before
// any resource is touched. No context, no backend, so this runs on every host
// rather than skipping on the Metal ones.

#include <gtest/gtest.h>

#include <irreden/ir_render.hpp>

#include <stdexcept>

namespace {

// Never registered by anything in the engine, so both lookups below are
// genuine misses rather than an ordering accident.
constexpr const char *kAbsentName = "IR_TEST_NeverRegisteredResource_2627";

class NamedResourceProbeTest : public testing::Test {
  protected:
    // Stamps IRRender::g_renderingResourceManager in its ctor, clears it in the
    // dtor, so the free-function API below resolves without a World.
    IRRender::RenderingResourceManager m_resource_manager;
};

TEST_F(NamedResourceProbeTest, ProbeReturnsNullForAnUnregisteredName) {
    EXPECT_EQ(IRRender::getNamedResourceOrNull<IRRender::Buffer>(kAbsentName), nullptr);
    EXPECT_EQ(IRRender::getNamedResourceOrNull<IRRender::ShaderProgram>(kAbsentName), nullptr);
}

#ifndef IR_RELEASE
// Positive control for the probe test: the same name through the asserting
// lookup throws. Together the two arms rule out the vacuous reading — a dead
// `getRenderingResourceManager()` would make this arm throw for the wrong
// reason and would stop the probe arm returning null at all.
TEST_F(NamedResourceProbeTest, AssertingLookupThrowsForTheSameName) {
    EXPECT_THROW(
        (void)IRRender::getNamedResource<IRRender::Buffer>(kAbsentName),
        std::runtime_error
    );
}

// The probe must not register the name it failed to find. `getCanvas` reaches
// into `m_canvasMap` with `operator[]`, which inserts on a miss; a probe with
// that shape would turn the first miss into a permanently-registered null id
// and make the asserting lookup stop asserting.
TEST_F(NamedResourceProbeTest, ProbingDoesNotRegisterTheMissingName) {
    ASSERT_EQ(IRRender::getNamedResourceOrNull<IRRender::Buffer>(kAbsentName), nullptr);

    EXPECT_EQ(IRRender::getNamedResourceOrNull<IRRender::Buffer>(kAbsentName), nullptr);
    EXPECT_THROW(
        (void)IRRender::getNamedResource<IRRender::Buffer>(kAbsentName),
        std::runtime_error
    );
}
#endif

} // namespace
