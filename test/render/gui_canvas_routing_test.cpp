// The GUI canvas reaches the screen through exactly one of two composites: the
// main gather (into the game-resolution framebuffer) or GUI_CANVAS_TO_SCREEN
// (at screen resolution). `IRPrefab::GuiCanvas::compositesOnScreen()` is the
// one decision both read, so these arms pin its three routing states:
//
//   * screen pass never registered — the gather keeps the canvas, so a
//     pipeline that predates the pass still shows its GUI;
//   * screen pass registered — the gather hands the canvas over;
//   * pixelated — the gather takes it back and the screen pass idles.
//
// A decision that read true with no screen pass would drop the GUI from both
// composites; one that ignored the pixelated setting would draw it twice.
//
// GL-free by construction: the arms register the system without calling its
// `create()` (which also builds the shader program and UBO), since the
// registry entry is all the routing reads.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/gui_canvas.hpp>
#include <irreden/render/systems/system_gui_canvas_to_screen.hpp>

namespace {

using ScreenPass = IRSystem::System<IRSystem::GUI_CANVAS_TO_SCREEN>;

class GuiCanvasRoutingTest : public testing::Test {
  protected:
    GuiCanvasRoutingTest()
        : m_entity_manager{}
        , m_system_manager{} {}

    static ScreenPass *registerScreenPass() {
        const IRSystem::SystemId id = IRSystem::registerSystem<
            IRSystem::GUI_CANVAS_TO_SCREEN,
            IRComponents::C_TriangleCanvasTextures,
            IRComponents::C_GuiCanvas>("GuiCanvasToScreen");
        return IRSystem::getSystemParams<ScreenPass>(id);
    }

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(GuiCanvasRoutingTest, GatherKeepsTheCanvasWhenTheScreenPassIsNotRegistered) {
    ASSERT_EQ(IRSystem::findSystem(IRSystem::GUI_CANVAS_TO_SCREEN), IRSystem::kNullSystemId)
        << "fixture precondition: nothing registered GUI_CANVAS_TO_SCREEN";

    EXPECT_FALSE(IRPrefab::GuiCanvas::isPixelated());
    EXPECT_FALSE(IRPrefab::GuiCanvas::compositesOnScreen());

    IRPrefab::GuiCanvas::setPixelated(true);
    EXPECT_FALSE(IRPrefab::GuiCanvas::compositesOnScreen());
}

TEST_F(GuiCanvasRoutingTest, RegisteredScreenPassTakesTheCanvasOffTheGather) {
    ScreenPass *screenPass = registerScreenPass();

    EXPECT_TRUE(IRPrefab::GuiCanvas::compositesOnScreen());

    screenPass->beginTick();
    EXPECT_TRUE(screenPass->compositesOnScreen_);
}

TEST_F(GuiCanvasRoutingTest, PixelatedReturnsTheCanvasToTheGatherAndIdlesTheScreenPass) {
    ScreenPass *screenPass = registerScreenPass();
    screenPass->beginTick();
    ASSERT_TRUE(screenPass->compositesOnScreen_)
        << "arm precondition: the screen pass draws before the setting flips";

    IRPrefab::GuiCanvas::setPixelated(true);

    EXPECT_TRUE(IRPrefab::GuiCanvas::isPixelated());
    EXPECT_FALSE(IRPrefab::GuiCanvas::compositesOnScreen());
    screenPass->beginTick();
    EXPECT_FALSE(screenPass->compositesOnScreen_);

    IRPrefab::GuiCanvas::setPixelated(false);

    EXPECT_TRUE(IRPrefab::GuiCanvas::compositesOnScreen());
    screenPass->beginTick();
    EXPECT_TRUE(screenPass->compositesOnScreen_);
}

// `World` applies the `gui_pixelated` config key once, at construction; a
// scene transition must not drop the canvas back onto the screen pass.
TEST_F(GuiCanvasRoutingTest, PixelatedSettingSurvivesSceneTransition) {
    registerScreenPass();
    IRPrefab::GuiCanvas::setPixelated(true);

    IREntity::resetGameplay();

    EXPECT_TRUE(IRPrefab::GuiCanvas::isPixelated());
    EXPECT_FALSE(IRPrefab::GuiCanvas::compositesOnScreen());
}

} // namespace
