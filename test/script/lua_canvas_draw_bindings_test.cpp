#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/render/canvas_draw.hpp>
#include <irreden/render/components/component_canvas_draw_queue.hpp>
#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/script/lua_canvas_draw_bindings.hpp>
#include <irreden/script/lua_script.hpp>

#include <gtest/gtest.h>

#include <map>
#include <stdexcept>
#include <string>

// The `IRCanvas` surface through the sol2 seam. The injected resolver and
// layer factory stand canvases up as bare `C_SizeTriangles` entities, so the
// suite runs without a RenderManager or a GPU; every draw lands in the
// `C_CanvasDrawQueue` singleton exactly as `CANVAS_DRAW` would flush it.

namespace {

using IRComponents::C_CanvasDrawQueue;
using IRComponents::C_SizeTriangles;
using IRMath::ivec2;

class LuaCanvasDrawBindingsTest : public testing::Test {
  protected:
    LuaCanvasDrawBindingsTest() {
        m_canvases["gui"] = IREntity::createEntity(C_SizeTriangles{ivec2(272, 962)});
        IRScript::detail::bindCanvasDraw(
            m_lua,
            [this](const std::string &name) -> IREntity::EntityId {
                const auto it = m_canvases.find(name);
                return it == m_canvases.end() ? IREntity::kNullEntity : it->second;
            },
            [this](const std::string &name) -> IREntity::EntityId {
                if (m_canvases.count(name) != 0) {
                    return IREntity::kNullEntity;
                }
                const IREntity::EntityId layer =
                    IREntity::createEntity(C_SizeTriangles{ivec2(272, 962)});
                m_canvases[name] = layer;
                return layer;
            }
        );
    }

    bool scriptSucceeds(const std::string &source) {
        return m_lua.lua().safe_script(source, sol::script_pass_on_error).valid();
    }

    std::string scriptError(const std::string &source) {
        sol::protected_function_result result =
            m_lua.lua().safe_script(source, sol::script_pass_on_error);
        EXPECT_FALSE(result.valid());
        if (result.valid()) {
            return {};
        }
        return sol::error(result).what();
    }

    const C_CanvasDrawQueue::Batch *batchFor(const std::string &name) {
        const IREntity::EntityId canvas = m_canvases.at(name);
        for (const C_CanvasDrawQueue::Batch &batch : IRPrefab::CanvasDraw::queue().batches_) {
            if (batch.canvas_ == canvas) {
                return &batch;
            }
        }
        return nullptr;
    }

    IRScript::LuaScript m_lua; // declared first: destroyed last
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
    std::map<std::string, IREntity::EntityId> m_canvases;
};

TEST_F(LuaCanvasDrawBindingsTest, ExposesTheSurfaceAndTheDepths) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        for _, name in ipairs({ 'createLayer', 'size', 'text', 'voxelText', 'measureVoxelText',
                                'fillRect', 'drawBorder' }) do
            assert(type(IRCanvas[name]) == 'function', name)
        end
        assert(IRCanvas.Depth.FRONT < IRCanvas.Depth.WORLD_NEAR)
        assert(IRCanvas.Depth.WORLD_NEAR < IRCanvas.Depth.WORLD_FAR)
        assert(IRCanvas.Depth.WORLD_FAR < IRCanvas.Depth.BEHIND)
        assert(IRCanvas.AlignH.CENTER == 1 and IRCanvas.AlignV.BOTTOM == 2)
    )lua"));
}

TEST_F(LuaCanvasDrawBindingsTest, TextQueuesOneCommandPerGlyphAtTheGivenDepth) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        IRCanvas.text('gui', 'AB C', 10, 20, { distance = IRCanvas.Depth.BEHIND, fontSize = 2 })
    )lua"));
    const C_CanvasDrawQueue::Batch *batch = batchFor("gui");
    ASSERT_NE(batch, nullptr);
    EXPECT_EQ(batch->canvasSize_, ivec2(272, 962));
    ASSERT_EQ(batch->commands_.size(), 4u);
    for (const IRRender::GlyphDrawCommand &cmd : batch->commands_) {
        EXPECT_EQ(static_cast<int>(cmd.distance), IRPrefab::CanvasDraw::kDepthBehind);
        EXPECT_EQ(cmd.styleFlags & 0xFFu, 2u);
    }
}

TEST_F(LuaCanvasDrawBindingsTest, TextDefaultsToTheFrontDepth) {
    EXPECT_TRUE(scriptSucceeds("IRCanvas.text('gui', 'X', 0, 0)"));
    const C_CanvasDrawQueue::Batch *batch = batchFor("gui");
    ASSERT_NE(batch, nullptr);
    ASSERT_EQ(batch->commands_.size(), 1u);
    EXPECT_EQ(static_cast<int>(batch->commands_[0].distance), IRPrefab::CanvasDraw::kDepthFront);
}

TEST_F(LuaCanvasDrawBindingsTest, RectIsTiledBySolidCellsAndClipped) {
    EXPECT_TRUE(scriptSucceeds("IRCanvas.fillRect('gui', 0, 0, 20, 30, { 1, 2, 3, 255 })"));
    const C_CanvasDrawQueue::Batch *batch = batchFor("gui");
    ASSERT_NE(batch, nullptr);
    // ceil(20 / 6) columns by ceil(30 / 11) rows of cells.
    ASSERT_EQ(batch->commands_.size(), 4u * 3u);
    for (const IRRender::GlyphDrawCommand &cmd : batch->commands_) {
        EXPECT_EQ(cmd.firstFontRow, static_cast<std::uint32_t>(IRPrefab::GuiText::kSolidCellStart));
        EXPECT_EQ(cmd.colorPacked, IRMath::Color(1, 2, 3, 255).toPackedRGBA());
    }
    const IRRender::GlyphDrawCommand &last = batch->commands_.back();
    EXPECT_EQ((last.styleFlags >> IRRender::kGlyphStyleCellWidthShift) & 0xFu, 2u);
    EXPECT_EQ((last.styleFlags >> IRRender::kGlyphStyleCellHeightShift) & 0xFu, 8u);

    IRPrefab::CanvasDraw::queue().batches_.clear();
    EXPECT_TRUE(scriptSucceeds("IRCanvas.fillRect('gui', 270, 0, 20, 5, { 0, 0, 0 }, 7)"));
    batch = batchFor("gui");
    ASSERT_NE(batch, nullptr);
    ASSERT_EQ(batch->commands_.size(), 1u);
    EXPECT_EQ((batch->commands_[0].styleFlags >> IRRender::kGlyphStyleCellWidthShift) & 0xFu, 2u);
    EXPECT_EQ(static_cast<int>(batch->commands_[0].distance), 7);

    IRPrefab::CanvasDraw::queue().batches_.clear();
    EXPECT_TRUE(scriptSucceeds("IRCanvas.fillRect('gui', -50, -50, 20, 20, { 0, 0, 0 })"));
    batch = batchFor("gui");
    EXPECT_TRUE(batch == nullptr || batch->commands_.empty());
}

TEST_F(LuaCanvasDrawBindingsTest, OverlappingDrawsSplitTheDispatchInCallOrder) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        IRCanvas.fillRect('gui', 10, 10, 60, 20, { 1, 1, 1 })
        IRCanvas.fillRect('gui', 100, 10, 20, 20, { 2, 2, 2 })
        IRCanvas.text('gui', 'HZ', 12, 14, { color = { 3, 3, 3 } })
        IRCanvas.fillRect('gui', -50, -50, 10, 10, { 4, 4, 4 })
    )lua"));
    const C_CanvasDrawQueue::Batch *batch = batchFor("gui");
    ASSERT_NE(batch, nullptr);
    // The clipped-away rectangle records no primitive.
    ASSERT_EQ(batch->primitives_.size(), 3u);
    // ceil(60 / 6) by ceil(20 / 11) cells, then ceil(20 / 6) by 2, then two glyphs.
    EXPECT_EQ(batch->primitives_[0].end_, 20u);
    EXPECT_EQ(batch->primitives_[1].end_, 28u);
    EXPECT_EQ(batch->primitives_[2].end_, batch->commands_.size());
    EXPECT_EQ(batch->primitives_[0].lo_, IRMath::ivec2(10, 9));
    EXPECT_EQ(batch->primitives_[0].hi_, IRMath::ivec2(71, 31));
    const std::span<const C_CanvasDrawQueue::Batch::Primitive> primitives(batch->primitives_);
    // The second rectangle is clear of the first; the text sits on the first.
    EXPECT_EQ(IRPrefab::CanvasDraw::overlapGroupEnd(primitives, 0), 2u);
    EXPECT_EQ(IRPrefab::CanvasDraw::overlapGroupEnd(primitives, 2), 3u);
}

TEST_F(LuaCanvasDrawBindingsTest, BorderIsFourStrips) {
    EXPECT_TRUE(scriptSucceeds("IRCanvas.drawBorder('gui', 0, 0, 12, 22, { 9, 9, 9 }, 1)"));
    const C_CanvasDrawQueue::Batch *batch = batchFor("gui");
    ASSERT_NE(batch, nullptr);
    // Top and bottom: 12 x 1 is two cells each; left and right: 1 x 20 is two cells each.
    EXPECT_EQ(batch->commands_.size(), 8u);
}

TEST_F(LuaCanvasDrawBindingsTest, VoxelTextQueuesFaceRunsAndMeasures) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local w, h = IRCanvas.measureVoxelText('AB', { voxelSize = 2 })
        assert(w > 0 and h > 0)
        local w1, h1 = IRCanvas.measureVoxelText('AB')
        assert(w > w1 and h > h1)
        IRCanvas.voxelText('gui', 'A', 4, 4, { color = { 200, 100, 50 }, voxelSize = 1,
                                              distance = IRCanvas.Depth.WORLD_NEAR })
    )lua"));
    const C_CanvasDrawQueue::Batch *batch = batchFor("gui");
    ASSERT_NE(batch, nullptr);
    ASSERT_FALSE(batch->commands_.empty());
    const IRRender::VoxelFaceColors faces =
        IRRender::shadedVoxelFaceColors(IRMath::Color(200, 100, 50, 255));
    bool sawTop = false;
    for (const IRRender::GlyphDrawCommand &cmd : batch->commands_) {
        EXPECT_EQ(cmd.firstFontRow, static_cast<std::uint32_t>(IRPrefab::GuiText::kSolidCellStart));
        EXPECT_EQ(static_cast<int>(cmd.distance), IRPrefab::CanvasDraw::kDepthWorldNear);
        sawTop = sawTop || cmd.colorPacked == faces.z_.toPackedRGBA();
    }
    EXPECT_TRUE(sawTop);
}

TEST_F(LuaCanvasDrawBindingsTest, VoxelRunsMeasurePlaceAndLightTheirCubes) {
    // Four unit cubes up the X axis: cells (0,0) .. (3,-3), the minimum
    // parity-shifted to (-1,-3): a 6 x 6 box, cube 0 at (1, 3).
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local w, h = IRCanvas.measureVoxelRun({ count = 4, voxelSize = 1, axis = IRCanvas.Axis.X })
        assert(w == 6 and h == 6, w .. 'x' .. h)
        local x, y, cw, ch = IRCanvas.voxelRunCell(1, { count = 4, voxelSize = 1, axis = IRCanvas.Axis.X })
        assert(x == 1 and y == 3 and cw == 2 and ch == 3, x .. ',' .. y)
        x, y = IRCanvas.voxelRunCell(4, { count = 4, voxelSize = 1, axis = IRCanvas.Axis.X })
        assert(x == 4 and y == 0, x .. ',' .. y)
        -- Three cubes of edge 2 level on screen: 12 wide, 6 tall plus the lattice pad.
        w, h = IRCanvas.measureVoxelRun({ count = 3, voxelSize = 2, axis = IRCanvas.Axis.SCREEN })
        assert(w == 12 and h == 10, w .. 'x' .. h)
        IRCanvas.voxelRun('gui', 20, 20, { count = 4, lit = 2, voxelSize = 1, axis = IRCanvas.Axis.X,
            color = { 100, 100, 100 }, unlitColor = { 10, 10, 10 }, distance = 5 })
    )lua"));
    const C_CanvasDrawQueue::Batch *batch = batchFor("gui");
    ASSERT_NE(batch, nullptr);
    ASSERT_EQ(batch->primitives_.size(), 1u);
    bool litTop = false;
    bool unlitTop = false;
    for (const IRRender::GlyphDrawCommand &cmd : batch->commands_) {
        EXPECT_EQ(static_cast<int>(cmd.distance), 5);
        litTop = litTop || cmd.colorPacked == IRMath::Color(100, 100, 100, 255).toPackedRGBA();
        unlitTop = unlitTop || cmd.colorPacked == IRMath::Color(10, 10, 10, 255).toPackedRGBA();
    }
    EXPECT_TRUE(litTop);
    EXPECT_TRUE(unlitTop);
    EXPECT_FALSE(scriptSucceeds("IRCanvas.voxelRun('gui', 0, 0, { count = 2, lit = 3 })"));
    EXPECT_FALSE(scriptSucceeds("IRCanvas.voxelRunCell(0, { count = 2 })"));
    EXPECT_FALSE(scriptSucceeds("IRCanvas.voxelRun('gui', 0, 0, { count = 2, axis = 7 })"));
}

TEST_F(LuaCanvasDrawBindingsTest, DiagonalVoxelRunsAreBoundedByTheirFootprint) {
    // An X or Y run of 125 cubes of edge 16 is a 2016 x 2064 box, the largest
    // under kMaxVoxelRunTexels; one more cube makes it 2032 x 2080.
    EXPECT_TRUE(scriptSucceeds(R"lua(
        for _, axis in ipairs({ IRCanvas.Axis.X, IRCanvas.Axis.Y }) do
            local shape = { count = 125, voxelSize = 16, axis = axis }
            local w, h = IRCanvas.measureVoxelRun(shape)
            assert(w == 2016 and h == 2064, w .. 'x' .. h)
            IRCanvas.voxelRunCell(125, shape)
            IRCanvas.voxelRun('gui', 0, 0, { count = 125, lit = 60, voxelSize = 16, axis = axis })
        end
        IRCanvas.voxelRun('gui', 0, 0, { count = 4096, voxelSize = 1, axis = IRCanvas.Axis.SCREEN })
    )lua"));
    const C_CanvasDrawQueue::Batch *batch = batchFor("gui");
    ASSERT_NE(batch, nullptr);
    EXPECT_EQ(batch->primitives_.size(), 3u);
    for (const char *axis : {"X", "Y"}) {
        const std::string shape =
            std::string("{ count = 126, voxelSize = 16, axis = IRCanvas.Axis.") + axis + " }";
        EXPECT_NE(
            scriptError("IRCanvas.voxelRun('gui', 0, 0, " + shape + ")").find("larger than"),
            std::string::npos
        );
        EXPECT_FALSE(scriptSucceeds("IRCanvas.measureVoxelRun(" + shape + ")"));
        EXPECT_FALSE(scriptSucceeds("IRCanvas.voxelRunCell(1, " + shape + ")"));
    }
    EXPECT_FALSE(
        scriptSucceeds("IRCanvas.voxelRun('gui', 0, 0, { count = 4096, voxelSize = 16 })")
    );
    EXPECT_EQ(batch->primitives_.size(), 3u);
}

TEST_F(LuaCanvasDrawBindingsTest, VoxelRunLayoutRefusesBadShapes) {
    IRRender::VoxelTextLayout layout;
    IRRender::VoxelTextScratch scratch;
    EXPECT_THROW(
        IRRender::layoutVoxelRun(0, 0, 1, IRRender::VoxelRunAxis::X, layout, scratch),
        std::runtime_error
    );
    EXPECT_THROW(
        IRRender::layoutVoxelRun(2, 3, 1, IRRender::VoxelRunAxis::X, layout, scratch),
        std::runtime_error
    );
    EXPECT_THROW(
        IRRender::layoutVoxelRun(2, 1, 0, IRRender::VoxelRunAxis::X, layout, scratch),
        std::runtime_error
    );
    EXPECT_THROW(
        IRRender::layoutVoxelRun(126, 0, 16, IRRender::VoxelRunAxis::X, layout, scratch),
        std::runtime_error
    );
    EXPECT_NO_THROW(IRRender::layoutVoxelRun(2, 2, 1, IRRender::VoxelRunAxis::Y, layout, scratch));
    EXPECT_EQ(layout.size_, IRRender::voxelRunSize(2, 1, IRRender::VoxelRunAxis::Y));
}

TEST_F(LuaCanvasDrawBindingsTest, LayersAreCreatedOnceAndSized) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        IRCanvas.createLayer('readout')
        local w, h = IRCanvas.size('readout')
        assert(w == 272 and h == 962)
        IRCanvas.text('readout', 'HI', 0, 0)
    )lua"));
    EXPECT_NE(batchFor("readout"), nullptr);
    EXPECT_NE(scriptError("IRCanvas.createLayer('readout')").find("exists"), std::string::npos);
    EXPECT_NE(scriptError("IRCanvas.createLayer('')").find("empty"), std::string::npos);
}

TEST_F(LuaCanvasDrawBindingsTest, RejectsUnknownCanvasesKeysAndRanges) {
    EXPECT_NE(
        scriptError("IRCanvas.text('nope', 'X', 0, 0)").find("no canvas named nope"),
        std::string::npos
    );
    EXPECT_NE(
        scriptError("IRCanvas.text('gui', 'X', 0, 0, { colour = { 1, 1, 1 } })")
            .find("unknown option"),
        std::string::npos
    );
    EXPECT_NE(
        scriptError("IRCanvas.text('gui', 'X', 0, 0, { fontSize = 0 })").find("fontSize"),
        std::string::npos
    );
    EXPECT_NE(
        scriptError("IRCanvas.text('gui', 'X', 0, 0, { size = 7 })").find("size must be"),
        std::string::npos
    );
    EXPECT_NE(
        scriptError("IRCanvas.voxelText('gui', 'X', 0, 0, { voxelSize = 17 })").find("voxelSize"),
        std::string::npos
    );
    EXPECT_NE(
        scriptError("IRCanvas.voxelText('gui', 'X', 0, 0, { facing = 9 })").find("facing"),
        std::string::npos
    );
    EXPECT_NE(
        scriptError("IRCanvas.text('gui', 'X', 0, 0, 5)").find("options must be a table"),
        std::string::npos
    );
    EXPECT_EQ(batchFor("gui"), nullptr);
}

} // namespace
