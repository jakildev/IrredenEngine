#include <gtest/gtest.h>

#include "../common/env_var.hpp"

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/script/lua_script.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_position_int_3d.hpp>
#include <irreden/common/components/component_size_int_3d.hpp>
#include <irreden/input/components/component_hitbox_2d.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_fog_exempt.hpp>
#include <irreden/render/components/component_fog_field.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_text_style.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_widget_image.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/systems/system_widget_input.hpp>
#include <irreden/render/widgets.hpp>
#include <irreden/update/components/component_angular_velocity.hpp>
#include <irreden/update/components/component_collision_response.hpp>
#include <irreden/update/components/component_goto_easing_3d.hpp>
#include <irreden/update/components/component_rotation_target.hpp>
#include <irreden/update/systems/system_angular_velocity_damped.hpp>
#include <irreden/update/systems/system_goto_3d.hpp>
#include <irreden/update/systems/system_rotation_target_local_transform.hpp>
#include <irreden/voxel/components/component_bind_points.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/world/world_snapshot.hpp>

#include <sol/sol.hpp>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

// The `IRPersist` Lua binding and optional IR_PERSIST_DUMP `.json.txt` debug
// dump. Drives the real Lua surface
// (`m_lua.bindLuaDrivenEcs()`) against a live EntityManager built like
// world_snapshot_test, so a Lua `saveWorld`/`loadWorld` round-trips real engine
// components through the process-default registry (C_LocalTransform,
// C_PositionInt3D, C_SizeInt3D — the trivially-copyable POD members, plus
// components with hand-written SaveSerialize<C> layouts).

namespace {

using IRComponents::C_AngularVelocity;
using IRComponents::C_BindPoints;
using IRComponents::C_CollisionResponse;
using IRComponents::C_EntityCanvas;
using IRComponents::C_FogExempt;
using IRComponents::C_FogField;
using IRComponents::C_GotoEasing3D;
using IRComponents::C_GuiPosition;
using IRComponents::C_HitBox2D;
using IRComponents::C_HitBox2DGui;
using IRComponents::C_LocalTransform;
using IRComponents::C_Name;
using IRComponents::C_PositionInt3D;
using IRComponents::C_RotationTarget;
using IRComponents::C_SizeInt3D;
using IRComponents::C_Skeleton;
using IRComponents::C_TextStyle;
using IRComponents::C_Voxel;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_Widget;
using IRComponents::C_WidgetButton;
using IRComponents::C_WidgetCheckbox;
using IRComponents::C_WidgetDropdown;
using IRComponents::C_WidgetImage;
using IRComponents::C_WidgetLabel;
using IRComponents::C_WidgetNumberField;
using IRComponents::C_WidgetSlider;
using IRComponents::C_WidgetState;
using IREntity::EntityId;
using IRRender::TextSize;

std::vector<std::uint8_t> readFileBytes(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()
    );
}

bool fileExists(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    return in.good();
}

// The flag-on / flag-off dump toggle, portable across the test hosts.
void setDumpFlag(bool on) {
    IRTest::setEnvVar("IR_PERSIST_DUMP", on ? "1" : nullptr);
}

class LuaWorldSnapshotTest : public testing::Test {
  protected:
    LuaWorldSnapshotTest()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{} {
        m_lua.bindLuaDrivenEcs();
    }

    void TearDown() override {
        setDumpFlag(false); // never leak the flag into a sibling test
    }

    bool evalTrue(const std::string &expr) {
        auto result = m_lua.lua().safe_script(
            "return (" + expr + ") and true or false",
            sol::script_pass_on_error
        );
        return result.valid() && result.get<bool>();
    }

    bool runOk(const std::string &stmt) {
        return m_lua.lua().safe_script(stmt, sol::script_pass_on_error).valid();
    }

    bool raisesError(const std::string &stmt) {
        return !m_lua.lua().safe_script(stmt, sol::script_pass_on_error).valid();
    }

    // The CHILD_OF parent of @p child, or kNullEntity (matches the query
    // world_snapshot_relations_test uses).
    EntityId parentOf(EntityId child) {
        return m_entity_manager.getParentEntityFromArchetype(
            m_entity_manager.getEntityArchetype(child)
        );
    }

    // Spliced into single-quoted Lua literals, so it must be free of
    // backslashes: Windows' native separator would read as an escape.
    std::string tempPath(const char *name) const {
        return (std::filesystem::path{testing::TempDir()} /
                (std::string{"ir_ws_lua_"} + name + ".irws"))
            .generic_string();
    }

    // One entity carrying one component column, written at @p version.
    void writeSingleColumnSnapshot(
        const std::string &path,
        EntityId entity,
        const char *componentName,
        std::uint32_t version,
        const std::vector<std::uint8_t> &column
    ) const {
        IRAsset::MemoryBinaryWriter componentNames;
        componentNames.writeVarUInt(1);
        componentNames.writeString(componentName);

        IRAsset::MemoryBinaryWriter archetypes;
        archetypes.writeVarUInt(1);
        archetypes.writeVarUInt(1);
        archetypes.writeVarUInt(0);
        archetypes.writeVarUInt(1);
        archetypes.writeVarUInt(entity);
        archetypes.writeU32(version);
        archetypes.writeVarUInt(column.size());
        archetypes.writeBytes(column.data(), column.size());

        IRAsset::MemoryBinaryWriter metadata;
        metadata.writeVarUInt(entity + 1);
        metadata.writeVarUInt(1);

        std::vector<IRAsset::ChunkPayload> chunks;
        chunks.push_back({IRAsset::makeTag("CMPN"), componentNames.takeBuffer()});
        chunks.push_back({IRAsset::makeTag("ARCH"), archetypes.takeBuffer()});
        chunks.push_back({IRAsset::makeTag("META"), metadata.takeBuffer()});

        IRAsset::FileBinaryWriter writer(path);
        ASSERT_TRUE(writer.ok());
        ASSERT_TRUE(
            IRAsset::writeChunked(
                writer,
                IRWorld::kWorldSnapshotMagic,
                IRWorld::kWorldSnapshotVersion,
                chunks
            )
                .ok()
        );
    }

    void writeHitBoxV1Snapshot(const std::string &path, EntityId entity) const {
        struct LegacyHitBox2D {
            IRMath::vec2 halfExtent_;
            bool hovered_;
        };
        static_assert(std::is_trivially_copyable_v<LegacyHitBox2D>);
        static_assert(sizeof(LegacyHitBox2D) == 12);

        const LegacyHitBox2D legacy{IRMath::vec2{6.0f, 4.0f}, true};
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(&legacy);
        writeSingleColumnSnapshot(
            path,
            entity,
            "IRComponents::C_HitBox2D",
            1,
            std::vector<std::uint8_t>(bytes, bytes + sizeof(legacy))
        );
    }

    void writeEntityCanvasV2Snapshot(const std::string &path, EntityId entity) const {
        struct LegacyEntityCanvas {
            EntityId canvasEntity_;
            IRMath::ivec2 canvasSize_;
            bool visible_;
            bool screenLocked_;
            int depthPriority_;
            float fogRevealFactor_;
            bool fogHidden_;
        };
        static_assert(std::is_trivially_copyable_v<LegacyEntityCanvas>);
        static_assert(sizeof(LegacyEntityCanvas) == 32);

        const LegacyEntityCanvas legacy{
            42,
            IRMath::ivec2{320, 180},
            false,
            true,
            7,
            0.0f,
            true,
        };
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(&legacy);
        writeSingleColumnSnapshot(
            path,
            entity,
            "IRComponents::C_EntityCanvas",
            2,
            std::vector<std::uint8_t>(bytes, bytes + sizeof(legacy))
        );
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(LuaWorldSnapshotTest, SurfaceBound) {
    EXPECT_TRUE(evalTrue("type(IRPersist) == 'table'"));
    EXPECT_TRUE(evalTrue("type(IRPersist.saveWorld) == 'function'"));
    EXPECT_TRUE(evalTrue("type(IRPersist.loadWorld) == 'function'"));
}

TEST_F(LuaWorldSnapshotTest, MigratesHitBoxV1AndRoundTripsAuthoredState) {
    const EntityId entity = 1000;
    const std::string oldPath = tempPath("hitbox_v1");
    const std::string currentPath = tempPath("hitbox_v2");
    ASSERT_NO_FATAL_FAILURE(writeHitBoxV1Snapshot(oldPath, entity));

    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + oldPath + "'))"));
    ASSERT_TRUE(m_entity_manager.entityExists(entity));
    C_HitBox2D &migrated = m_entity_manager.getComponent<C_HitBox2D>(entity);
    EXPECT_EQ(migrated.halfExtent_, IRMath::vec2(6.0f, 4.0f));
    EXPECT_TRUE(migrated.hovered_);
    EXPECT_FLOAT_EQ(migrated.padding_, IRConstants::kDefaultPickPadding);
    EXPECT_TRUE(migrated.enabled_);

    migrated.padding_ = 9.0f;
    migrated.enabled_ = false;
    migrated.pickPriority_ = 12;
    migrated.screenSpaceCenter_ = true;
    migrated.screenSpacePlaced_ = true;
    migrated.centerScreen_ = IRMath::vec2{100.0f, 200.0f};
    migrated.isoDepth_ = 31;
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + currentPath + "'))"));

    m_entity_manager.destroyAllEntities();
    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + currentPath + "'))"));
    const C_HitBox2D &restored = m_entity_manager.getComponent<C_HitBox2D>(entity);
    EXPECT_EQ(restored.halfExtent_, IRMath::vec2(6.0f, 4.0f));
    EXPECT_FLOAT_EQ(restored.padding_, 9.0f);
    EXPECT_FALSE(restored.enabled_);
    EXPECT_EQ(restored.pickPriority_, 12);
    EXPECT_FALSE(restored.hovered_);
    EXPECT_FALSE(restored.screenSpaceCenter_);
    EXPECT_FALSE(restored.screenSpacePlaced_);
    EXPECT_EQ(restored.centerScreen_, IRMath::vec2(0.0f));
    EXPECT_EQ(restored.isoDepth_, 0);
}

TEST_F(LuaWorldSnapshotTest, MigratesEntityCanvasV2AndDropsDerivedFogState) {
    const EntityId entity = 1001;
    const std::string oldPath = tempPath("entity_canvas_v2");
    const std::string currentPath = tempPath("entity_canvas_v3");
    ASSERT_NO_FATAL_FAILURE(writeEntityCanvasV2Snapshot(oldPath, entity));

    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + oldPath + "'))"));
    ASSERT_TRUE(m_entity_manager.entityExists(entity));
    C_EntityCanvas &migrated = m_entity_manager.getComponent<C_EntityCanvas>(entity);
    EXPECT_EQ(migrated.canvasEntity_, 42u);
    EXPECT_EQ(migrated.canvasSize_, IRMath::ivec2(320, 180));
    EXPECT_FALSE(migrated.visible_);
    EXPECT_TRUE(migrated.screenLocked_);
    EXPECT_EQ(migrated.depthPriority_, 7);
    EXPECT_FLOAT_EQ(migrated.fogRevealFactor_, 1.0f);
    EXPECT_FALSE(migrated.fogHidden_);

    migrated.fogRevealFactor_ = 0.0f;
    migrated.fogHidden_ = true;
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + currentPath + "'))"));

    m_entity_manager.destroyAllEntities();
    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + currentPath + "'))"));
    const C_EntityCanvas &restored = m_entity_manager.getComponent<C_EntityCanvas>(entity);
    EXPECT_EQ(restored.canvasEntity_, 42u);
    EXPECT_EQ(restored.canvasSize_, IRMath::ivec2(320, 180));
    EXPECT_FALSE(restored.visible_);
    EXPECT_TRUE(restored.screenLocked_);
    EXPECT_EQ(restored.depthPriority_, 7);
    EXPECT_FLOAT_EQ(restored.fogRevealFactor_, 1.0f);
    EXPECT_FALSE(restored.fogHidden_);
}

// Every component that stores a glyph-set size keeps SMALL across a save and
// load through the Lua surface; a reader that dropped the field reads LARGE.
TEST_F(LuaWorldSnapshotTest, RoundTripsTheSmallTextSize) {
    const EntityId style = m_entity_manager.createEntity(
        C_TextStyle{
            IRMath::IRColors::kWhite,
            0,
            IRComponents::TextAlignH::CENTER,
            IRComponents::TextAlignV::BOTTOM,
            80,
            20,
            1,
            TextSize::SMALL,
        }
    );
    C_WidgetLabel label{};
    label.text_ = "LABEL";
    label.textSize_ = TextSize::SMALL;
    const EntityId labelEntity = m_entity_manager.createEntity(label);
    C_WidgetButton button{};
    button.label_ = "BUTTON";
    button.textSize_ = TextSize::SMALL;
    const EntityId buttonEntity = m_entity_manager.createEntity(button);
    C_WidgetSlider slider{};
    slider.label_ = "GAIN";
    slider.defaultValue_ = 0.25f;
    slider.step_ = 0.05f;
    slider.textSize_ = TextSize::SMALL;
    const EntityId sliderEntity = m_entity_manager.createEntity(slider);
    C_WidgetCheckbox checkbox{};
    checkbox.label_ = "LOCK";
    checkbox.textSize_ = TextSize::SMALL;
    const EntityId checkboxEntity = m_entity_manager.createEntity(checkbox);
    C_WidgetDropdown dropdown{};
    dropdown.items_ = {"RAMP", "SINE"};
    dropdown.textSize_ = TextSize::SMALL;
    const EntityId dropdownEntity = m_entity_manager.createEntity(dropdown);
    C_WidgetNumberField numberField{};
    numberField.value_ = 3.5f;
    numberField.defaultValue_ = 3.5f;
    numberField.minValue_ = 1.0f;
    numberField.maxValue_ = 8.0f;
    numberField.step_ = 0.5f;
    numberField.textSize_ = TextSize::SMALL;
    numberField.editing_ = true;
    numberField.editBuffer_ = "7.5";
    const EntityId numberFieldEntity = m_entity_manager.createEntity(numberField);

    const std::string path = tempPath("text_sizes");
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + path + "'))"));
    m_entity_manager.destroyAllEntities();
    ASSERT_EQ(m_entity_manager.getLiveEntityCount(), 0u);
    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + path + "'))"));

    const C_TextStyle &restoredStyle = m_entity_manager.getComponent<C_TextStyle>(style);
    EXPECT_EQ(restoredStyle.textSize_, TextSize::SMALL);
    EXPECT_EQ(restoredStyle.alignH_, IRComponents::TextAlignH::CENTER);
    EXPECT_EQ(restoredStyle.alignV_, IRComponents::TextAlignV::BOTTOM);
    EXPECT_EQ(restoredStyle.boxWidth_, 80);
    EXPECT_EQ(restoredStyle.fontSize_, 1);
    EXPECT_EQ(m_entity_manager.getComponent<C_WidgetLabel>(labelEntity).textSize_, TextSize::SMALL);
    EXPECT_EQ(m_entity_manager.getComponent<C_WidgetLabel>(labelEntity).text_, "LABEL");
    EXPECT_EQ(
        m_entity_manager.getComponent<C_WidgetButton>(buttonEntity).textSize_,
        TextSize::SMALL
    );
    EXPECT_EQ(
        m_entity_manager.getComponent<C_WidgetSlider>(sliderEntity).textSize_,
        TextSize::SMALL
    );
    EXPECT_FLOAT_EQ(
        m_entity_manager.getComponent<C_WidgetSlider>(sliderEntity).defaultValue_,
        0.25f
    );
    EXPECT_FLOAT_EQ(m_entity_manager.getComponent<C_WidgetSlider>(sliderEntity).step_, 0.05f);
    EXPECT_EQ(
        m_entity_manager.getComponent<C_WidgetCheckbox>(checkboxEntity).textSize_,
        TextSize::SMALL
    );
    EXPECT_EQ(
        m_entity_manager.getComponent<C_WidgetDropdown>(dropdownEntity).textSize_,
        TextSize::SMALL
    );
    const C_WidgetNumberField &restoredNumber =
        m_entity_manager.getComponent<C_WidgetNumberField>(numberFieldEntity);
    EXPECT_FLOAT_EQ(restoredNumber.value_, 3.5f);
    EXPECT_FLOAT_EQ(restoredNumber.defaultValue_, 3.5f);
    EXPECT_FLOAT_EQ(restoredNumber.minValue_, 1.0f);
    EXPECT_FLOAT_EQ(restoredNumber.maxValue_, 8.0f);
    EXPECT_FLOAT_EQ(restoredNumber.step_, 0.5f);
    EXPECT_EQ(restoredNumber.textSize_, TextSize::SMALL);
    EXPECT_FALSE(restoredNumber.editing_);
    EXPECT_TRUE(restoredNumber.editBuffer_.empty());
}

TEST_F(LuaWorldSnapshotTest, WidgetResetLoadRestoresAuthoredValuesButClearsTransientState) {
    const IRSystem::SystemId inputSystem = IRSystem::createSystem<IRSystem::WIDGET_INPUT>();
    auto *input =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::WIDGET_INPUT>>(inputSystem);
    ASSERT_NE(input, nullptr);

    const EntityId field = IRPrefab::Widget::makeNumberField(
        IRMath::ivec2(210, 20),
        IRMath::ivec2(60, 18),
        0.06f,
        0.0f,
        1.0f,
        0.1f
    );
    const EntityId slider = IRPrefab::Widget::makeSlider(
        IRMath::ivec2(20),
        IRMath::ivec2(180, 30),
        "GAIN",
        0.0f,
        1.0f,
        0.7f,
        TextSize::SMALL,
        IRPrefab::Widget::SliderOptions{0.25f, 0.05f, field}
    );
    const EntityId pending = IRPrefab::Widget::makeNumberField(
        IRMath::ivec2(280, 20),
        IRMath::ivec2(60, 18),
        0.5f,
        0.0f,
        1.0f,
        0.1f
    );

    C_WidgetState &active = m_entity_manager.getComponent<C_WidgetState>(slider);
    active.hovered_ = true;
    active.pressed_ = true;
    active.focused_ = true;
    active.dragValue_ = 0.9f;
    active.sliderPressStarted_ = true;
    active.sliderFirstClickRegion_ = C_WidgetState::SliderPressRegion::TRACK;
    active.sliderClickAge_ = 4;
    input->capturedWidgetId_ = slider;
    input->focusedWidgetId_ = field;
    input->topHoveredId_ = slider;
    input->focusableWidgets_ = {slider};
    IRPrefab::Widget::focus(pending);

    const std::string path = tempPath("widget_reset_load");
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + path + "'))"));
    IREntity::resetGameplay();
    EXPECT_EQ(input->capturedWidgetId_, IREntity::kNullEntity);
    EXPECT_EQ(input->focusedWidgetId_, IREntity::kNullEntity);
    EXPECT_EQ(input->topHoveredId_, IREntity::kNullEntity);
    EXPECT_TRUE(input->focusableWidgets_.empty());
    EXPECT_EQ(IRPrefab::Widget::takeFocusRequest(), IREntity::kNullEntity);

    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + path + "'))"));
    const C_WidgetSlider &restoredSlider = m_entity_manager.getComponent<C_WidgetSlider>(slider);
    EXPECT_FLOAT_EQ(restoredSlider.currentValue_, 0.7f);
    EXPECT_FLOAT_EQ(restoredSlider.defaultValue_, 0.25f);
    EXPECT_FLOAT_EQ(restoredSlider.step_, 0.05f);
    EXPECT_EQ(restoredSlider.fieldId_, field);
    const C_WidgetNumberField &restoredField =
        m_entity_manager.getComponent<C_WidgetNumberField>(field);
    EXPECT_FLOAT_EQ(restoredField.value_, 0.1f);
    EXPECT_FLOAT_EQ(restoredField.defaultValue_, 0.1f);
    C_WidgetState &sliderState = m_entity_manager.getComponent<C_WidgetState>(slider);
    C_WidgetState &fieldState = m_entity_manager.getComponent<C_WidgetState>(field);
    input->tick(
        slider,
        m_entity_manager.getComponent<C_Widget>(slider),
        sliderState,
        m_entity_manager.getComponent<C_HitBox2DGui>(slider),
        m_entity_manager.getComponent<C_GuiPosition>(slider)
    );
    input->tick(
        field,
        m_entity_manager.getComponent<C_Widget>(field),
        fieldState,
        m_entity_manager.getComponent<C_HitBox2DGui>(field),
        m_entity_manager.getComponent<C_GuiPosition>(field)
    );
    EXPECT_EQ(input->capturedWidgetId_, IREntity::kNullEntity);
    EXPECT_EQ(input->focusedWidgetId_, IREntity::kNullEntity);
    EXPECT_EQ(IRPrefab::Widget::takeFocusRequest(), IREntity::kNullEntity);
    EXPECT_FALSE(sliderState.hovered_);
    EXPECT_FALSE(sliderState.pressed_);
    EXPECT_FALSE(sliderState.focused_);
    EXPECT_FALSE(sliderState.sliderPressStarted_);
    EXPECT_EQ(sliderState.sliderFirstClickRegion_, C_WidgetState::SliderPressRegion::NONE);
    EXPECT_FALSE(fieldState.focused_);
}

TEST_F(LuaWorldSnapshotTest, WidgetRetiredColumnsDeriveAuthoredValuesThroughLua) {
    const EntityId entity = 1002;
    {
        IRAsset::MemoryBinaryWriter sliderV1;
        sliderV1.writeString("Zoom");
        sliderV1.writeF32(-2.0f);
        sliderV1.writeF32(8.0f);
        sliderV1.writeF32(3.25f);
        const std::string path = tempPath("widget_slider_v1");
        ASSERT_NO_FATAL_FAILURE(writeSingleColumnSnapshot(
            path,
            entity,
            "IRComponents::C_WidgetSlider",
            1,
            sliderV1.buffer()
        ));
        ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + path + "'))"));
        const C_WidgetSlider &slider = m_entity_manager.getComponent<C_WidgetSlider>(entity);
        EXPECT_FLOAT_EQ(slider.currentValue_, 3.25f);
        EXPECT_FLOAT_EQ(slider.defaultValue_, 3.25f);
        EXPECT_FLOAT_EQ(slider.step_, 0.1f);
        EXPECT_EQ(slider.fieldId_, IREntity::kNullEntity);
        EXPECT_EQ(slider.textSize_, TextSize::LARGE);
    }

    m_entity_manager.destroyAllEntities();
    {
        IRAsset::MemoryBinaryWriter sliderV2;
        sliderV2.writeString("Zoom");
        sliderV2.writeF32(-2.0f);
        sliderV2.writeF32(8.0f);
        sliderV2.writeF32(3.25f);
        sliderV2.writeU8(static_cast<std::uint8_t>(TextSize::SMALL));
        const std::string path = tempPath("widget_slider_v2");
        ASSERT_NO_FATAL_FAILURE(writeSingleColumnSnapshot(
            path,
            entity,
            "IRComponents::C_WidgetSlider",
            2,
            sliderV2.buffer()
        ));
        ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + path + "'))"));
        const C_WidgetSlider &slider = m_entity_manager.getComponent<C_WidgetSlider>(entity);
        EXPECT_FLOAT_EQ(slider.currentValue_, 3.25f);
        EXPECT_FLOAT_EQ(slider.defaultValue_, 3.25f);
        EXPECT_FLOAT_EQ(slider.step_, 0.1f);
        EXPECT_EQ(slider.fieldId_, IREntity::kNullEntity);
        EXPECT_EQ(slider.textSize_, TextSize::SMALL);
    }

    m_entity_manager.destroyAllEntities();
    IRAsset::MemoryBinaryWriter numberFieldV1;
    numberFieldV1.writeF32(3.5f);
    numberFieldV1.writeF32(-2.0f);
    numberFieldV1.writeF32(8.0f);
    numberFieldV1.writeF32(0.5f);
    numberFieldV1.writeU8(static_cast<std::uint8_t>(TextSize::SMALL));
    const std::string path = tempPath("widget_number_field_v1");
    ASSERT_NO_FATAL_FAILURE(writeSingleColumnSnapshot(
        path,
        entity,
        "IRComponents::C_WidgetNumberField",
        1,
        numberFieldV1.buffer()
    ));
    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + path + "'))"));
    const C_WidgetNumberField &field = m_entity_manager.getComponent<C_WidgetNumberField>(entity);
    EXPECT_FLOAT_EQ(field.value_, 3.5f);
    EXPECT_FLOAT_EQ(field.defaultValue_, 3.5f);
    EXPECT_FLOAT_EQ(field.step_, 0.5f);
    EXPECT_EQ(field.textSize_, TextSize::SMALL);
    EXPECT_FALSE(field.editing_);
    EXPECT_TRUE(field.editBuffer_.empty());
}

// Rows saved before the glyph-set size existed load through the retired
// readers as LARGE with every other field intact.
TEST_F(LuaWorldSnapshotTest, MigratesVersionOneTextRowsAsLarge) {
    struct LegacyTextStyle {
        IRMath::Color color_;
        int wrapWidth_;
        IRComponents::TextAlignH alignH_;
        IRComponents::TextAlignV alignV_;
        int boxWidth_;
        int boxHeight_;
        int fontSize_;
    };
    static_assert(std::is_trivially_copyable_v<LegacyTextStyle>);
    static_assert(sizeof(LegacyTextStyle) == 28);
    const LegacyTextStyle legacy{
        IRMath::Color{5, 6, 7, 8},
        -1,
        IRComponents::TextAlignH::RIGHT,
        IRComponents::TextAlignV::CENTER,
        64,
        32,
        2,
    };
    const auto *legacyBytes = reinterpret_cast<const std::uint8_t *>(&legacy);
    const EntityId styleEntity = 1002;
    const std::string stylePath = tempPath("text_style_v1");
    ASSERT_NO_FATAL_FAILURE(writeSingleColumnSnapshot(
        stylePath,
        styleEntity,
        "IRComponents::C_TextStyle",
        1,
        std::vector<std::uint8_t>(legacyBytes, legacyBytes + sizeof(legacy))
    ));
    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + stylePath + "'))"));
    ASSERT_TRUE(m_entity_manager.entityExists(styleEntity));
    const C_TextStyle &style = m_entity_manager.getComponent<C_TextStyle>(styleEntity);
    EXPECT_EQ(style.color_.green_, 6);
    EXPECT_EQ(style.wrapWidth_, -1);
    EXPECT_EQ(style.alignH_, IRComponents::TextAlignH::RIGHT);
    EXPECT_EQ(style.alignV_, IRComponents::TextAlignV::CENTER);
    EXPECT_EQ(style.boxWidth_, 64);
    EXPECT_EQ(style.boxHeight_, 32);
    EXPECT_EQ(style.fontSize_, 2);
    EXPECT_EQ(style.textSize_, TextSize::LARGE);

    m_entity_manager.destroyAllEntities();
    IRAsset::MemoryBinaryWriter labelV1;
    labelV1.writeString("Frames/sec");
    labelV1.writeU32(IRMath::Color{9, 8, 7, 240}.toPackedRGBA());
    const EntityId labelEntity = 1003;
    const std::string labelPath = tempPath("widget_label_v1");
    ASSERT_NO_FATAL_FAILURE(writeSingleColumnSnapshot(
        labelPath,
        labelEntity,
        "IRComponents::C_WidgetLabel",
        1,
        labelV1.buffer()
    ));
    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + labelPath + "'))"));
    ASSERT_TRUE(m_entity_manager.entityExists(labelEntity));
    const C_WidgetLabel &label = m_entity_manager.getComponent<C_WidgetLabel>(labelEntity);
    EXPECT_EQ(label.text_, "Frames/sec");
    EXPECT_EQ(label.colorOverride_.alpha_, 240);
    EXPECT_EQ(label.textSize_, TextSize::LARGE);
}

// The layout-audit cover flag survives a save and load through the Lua
// surface, and a version-1 raw image takes its kind's builder default.
TEST_F(LuaWorldSnapshotTest, WidgetCoverRoundTripsAndMigratesFromVersionOne) {
    C_Widget covering{IRComponents::WidgetKind::BUTTON, IRMath::ivec2(40, 14)};
    covering.coversBelow_ = true;
    C_Widget uncovered{IRComponents::WidgetKind::PANEL, IRMath::ivec2(100, 60)};
    const EntityId coveringEntity = m_entity_manager.createEntity(covering);
    const EntityId uncoveredEntity = m_entity_manager.createEntity(uncovered);
    const std::string currentPath = tempPath("widget_v2");
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + currentPath + "'))"));

    m_entity_manager.destroyAllEntities();
    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + currentPath + "'))"));
    EXPECT_TRUE(m_entity_manager.getComponent<C_Widget>(coveringEntity).coversBelow_);
    EXPECT_FALSE(m_entity_manager.getComponent<C_Widget>(uncoveredEntity).coversBelow_);

    m_entity_manager.destroyAllEntities();
    struct LegacyWidget {
        IRComponents::WidgetKind kind_;
        IRMath::ivec2 size_;
        bool disabled_;
        int zOrder_;
    };
    static_assert(std::is_trivially_copyable_v<LegacyWidget>);
    static_assert(sizeof(LegacyWidget) == 20);
    for (const IRComponents::WidgetKind kind :
         {IRComponents::WidgetKind::DROPDOWN, IRComponents::WidgetKind::SLIDER}) {
        std::array<std::uint8_t, sizeof(LegacyWidget)> image{};
        image.fill(0xFF);
        const LegacyWidget legacy{kind, IRMath::ivec2(90, 18), false, 2};
        std::memcpy(image.data(), &legacy.kind_, sizeof(legacy.kind_));
        std::memcpy(
            image.data() + offsetof(LegacyWidget, size_),
            &legacy.size_,
            sizeof(legacy.size_)
        );
        std::memcpy(
            image.data() + offsetof(LegacyWidget, disabled_),
            &legacy.disabled_,
            sizeof(legacy.disabled_)
        );
        std::memcpy(
            image.data() + offsetof(LegacyWidget, zOrder_),
            &legacy.zOrder_,
            sizeof(legacy.zOrder_)
        );
        const EntityId entity = 1004;
        const std::string oldPath = tempPath("widget_v1");
        ASSERT_NO_FATAL_FAILURE(writeSingleColumnSnapshot(
            oldPath,
            entity,
            "IRComponents::C_Widget",
            1,
            std::vector<std::uint8_t>(image.begin(), image.end())
        ));
        ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + oldPath + "'))"));
        ASSERT_TRUE(m_entity_manager.entityExists(entity));
        const C_Widget &migrated = m_entity_manager.getComponent<C_Widget>(entity);
        EXPECT_EQ(migrated.kind_, kind);
        EXPECT_EQ(migrated.size_, IRMath::ivec2(90, 18));
        EXPECT_FALSE(migrated.disabled_);
        EXPECT_EQ(migrated.zOrder_, 2);
        EXPECT_EQ(migrated.coversBelow_, kind == IRComponents::WidgetKind::DROPDOWN);
        m_entity_manager.destroyAllEntities();
    }
}

// The core W-9 acceptance: a non-trivial world (multiple archetypes +
// CHILD_OF) built in C++, saved and reloaded entirely through the Lua binding,
// with exact component + relation parity on restore.
TEST_F(LuaWorldSnapshotTest, RoundTripThroughLua) {
    // Archetype {C_LocalTransform} — the relation parent + a bare peer.
    const EntityId parent =
        m_entity_manager.createEntity(C_LocalTransform{IRMath::vec3(1.0f, 2.0f, 3.0f)});
    const EntityId peer =
        m_entity_manager.createEntity(C_LocalTransform{IRMath::vec3(-4.0f, 5.0f, 6.0f)});
    // Archetype {C_PositionInt3D, C_SizeInt3D} — the relation child.
    const EntityId child =
        m_entity_manager.createEntity(C_PositionInt3D{4, 5, 6}, C_SizeInt3D{7, 8, 9});
    // Archetype {C_PositionInt3D} and {C_LocalTransform, C_PositionInt3D} so the
    // save spans 4 distinct projected archetypes.
    const EntityId gridOnly = m_entity_manager.createEntity(C_PositionInt3D{10, 11, 12});
    const EntityId both = m_entity_manager.createEntity(
        C_LocalTransform{IRMath::vec3(7.0f, 8.0f, 9.0f)},
        C_PositionInt3D{13, 14, 15}
    );
    IREntity::setParent(child, parent);
    ASSERT_EQ(parentOf(child), parent);

    const std::string path = tempPath("roundtrip");
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + path + "'))"))
        << "Lua IRPersist.saveWorld raised or returned false";

    // Clear the world (the reset the loadWorld contract expects), then reload
    // through the Lua binding.
    m_entity_manager.destroyAllEntities();
    ASSERT_EQ(m_entity_manager.getLiveEntityCount(), 0u);
    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + path + "'))"))
        << "Lua IRPersist.loadWorld raised or returned false";

    // Exact-id restoration + component parity.
    ASSERT_TRUE(m_entity_manager.entityExists(parent));
    ASSERT_TRUE(m_entity_manager.entityExists(child));
    ASSERT_TRUE(m_entity_manager.entityExists(peer));
    ASSERT_TRUE(m_entity_manager.entityExists(gridOnly));
    ASSERT_TRUE(m_entity_manager.entityExists(both));

    const C_LocalTransform &parentXform = m_entity_manager.getComponent<C_LocalTransform>(parent);
    EXPECT_FLOAT_EQ(parentXform.translation_.x, 1.0f);
    EXPECT_FLOAT_EQ(parentXform.translation_.y, 2.0f);
    EXPECT_FLOAT_EQ(parentXform.translation_.z, 3.0f);

    const IRMath::ivec3 childPos = m_entity_manager.getComponent<C_PositionInt3D>(child).pos_;
    EXPECT_EQ(childPos.x, 4);
    EXPECT_EQ(childPos.y, 5);
    EXPECT_EQ(childPos.z, 6);
    const IRMath::ivec3 childSize = m_entity_manager.getComponent<C_SizeInt3D>(child).size_;
    EXPECT_EQ(childSize.x, 7);
    EXPECT_EQ(childSize.y, 8);
    EXPECT_EQ(childSize.z, 9);
    const IRMath::ivec3 gridPos = m_entity_manager.getComponent<C_PositionInt3D>(gridOnly).pos_;
    EXPECT_EQ(gridPos.x, 10);
    EXPECT_EQ(gridPos.z, 12);
    const IRMath::ivec3 bothPos = m_entity_manager.getComponent<C_PositionInt3D>(both).pos_;
    EXPECT_EQ(bothPos.x, 13);
    EXPECT_EQ(bothPos.z, 15);

    // CHILD_OF edge restored.
    EXPECT_EQ(parentOf(child), parent) << "CHILD_OF edge lost on reload";
}

// `makeDefaultSaveRegistry()` registers C_VoxelSetNew (world_default_registry.cpp)
// alongside the trivially-copyable PODs above — the one component with an
// explicit SaveSerialize<C> rather than the default
// byte-copy path. Round-trips a headless, pool-free set (StagedInit — no
// VoxelPool/canvas needed) through the Lua binding; reload always
// reconstructs in staged mode (numVoxels_ == 0, pendingVoxels_ populated) —
// attaching it to a live pool is a separate step the caller's UPDATE
// pipeline performs via SEED_STAGED_VOXELS (lua_world_snapshot_bindings.hpp).
TEST_F(LuaWorldSnapshotTest, RoundTripsVoxelSetNew) {
    const IRMath::ivec3 size{2, 1, 2};
    const IRMath::ivec3 boundsMin{3, -1, 5};
    const IREntity::EntityId canvas = 777;
    std::vector<C_Voxel> voxels(4);
    for (std::size_t i = 0; i < voxels.size(); ++i) {
        voxels[i].color_ = IRMath::Color{
            static_cast<std::uint8_t>(i * 10 + 1),
            static_cast<std::uint8_t>(i * 20 + 2),
            static_cast<std::uint8_t>(i * 30 + 3),
            255
        };
    }

    const EntityId entity = m_entity_manager.createEntity(
        C_VoxelSetNew{C_VoxelSetNew::StagedInit{}, size, boundsMin, voxels, canvas}
    );

    const std::string path = tempPath("voxelset");
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + path + "'))"));

    m_entity_manager.destroyAllEntities();
    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + path + "'))"));

    ASSERT_TRUE(m_entity_manager.entityExists(entity));
    const C_VoxelSetNew &reloaded = m_entity_manager.getComponent<C_VoxelSetNew>(entity);
    EXPECT_EQ(reloaded.size_.x, size.x);
    EXPECT_EQ(reloaded.size_.y, size.y);
    EXPECT_EQ(reloaded.size_.z, size.z);
    EXPECT_EQ(reloaded.pendingBoundsMin_.x, boundsMin.x);
    EXPECT_EQ(reloaded.pendingBoundsMin_.y, boundsMin.y);
    EXPECT_EQ(reloaded.pendingBoundsMin_.z, boundsMin.z);
    EXPECT_EQ(reloaded.canvasEntity_, canvas);
    EXPECT_EQ(reloaded.numVoxels_, 0) << "reload must stay staged — never touches a pool";
    ASSERT_EQ(reloaded.pendingVoxels_.size(), voxels.size());
    for (std::size_t i = 0; i < voxels.size(); ++i) {
        EXPECT_EQ(0, std::memcmp(&reloaded.pendingVoxels_[i], &voxels[i], sizeof(C_Voxel)))
            << "voxel record " << i << " differs after round-trip";
    }
}

// The fog subject-class markers are authored classification: a voxel set that
// reloads without its marker is silently adopted as the default BODY class, so
// each marker must survive the Lua round trip and read back its class, and a
// marker-less set must still read BODY.
TEST_F(LuaWorldSnapshotTest, RoundTripsFogSubjectClassMarkers) {
    const IRMath::ivec3 size{1, 1, 1};
    const std::vector<C_Voxel> voxels(1);
    const auto stagedSet = [&]() {
        return C_VoxelSetNew{C_VoxelSetNew::StagedInit{}, size, IRMath::ivec3(0), voxels, 777};
    };
    const EntityId field = m_entity_manager.createEntity(stagedSet(), C_FogField{});
    const EntityId exempt = m_entity_manager.createEntity(stagedSet(), C_FogExempt{});
    const EntityId body = m_entity_manager.createEntity(stagedSet());

    const std::string path = tempPath("fog_subject_class");
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + path + "'))"));

    m_entity_manager.destroyAllEntities();
    ASSERT_EQ(m_entity_manager.getLiveEntityCount(), 0u);
    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + path + "'))"));

    using IRPrefab::Fog::FogSubjectClass;
    ASSERT_TRUE(m_entity_manager.entityExists(field));
    EXPECT_TRUE(m_entity_manager.getComponentOptional<C_FogField>(field).has_value());
    EXPECT_EQ(IRPrefab::Fog::subjectClass(field), FogSubjectClass::FIELD);
    ASSERT_TRUE(m_entity_manager.entityExists(exempt));
    EXPECT_TRUE(m_entity_manager.getComponentOptional<C_FogExempt>(exempt).has_value());
    EXPECT_EQ(IRPrefab::Fog::subjectClass(exempt), FogSubjectClass::EXEMPT);
    ASSERT_TRUE(m_entity_manager.entityExists(body));
    EXPECT_EQ(IRPrefab::Fog::subjectClass(body), FogSubjectClass::BODY);
}

// The process-default registry derives its membership
// from the whole opted-in inventory, so heap-owning components round-trip
// through the Lua binding too — not just the four hand-curated PODs P7
// shipped with. C_Name is the issue's named criterion; C_WidgetLabel and
// C_Skeleton cover the other two container shapes the serializers introduce
// (string + packed color, and two independent vectors).
//
// This is the wiring-level test `engine/world/CLAUDE.md` requires for any
// component reachable through the default registry: the per-serializer
// unit coverage in test/world/save_serializers_test.cpp proves the bytes, but
// only a run through the real IRPersist surface proves registry entry ->
// binding -> reload.
TEST_F(LuaWorldSnapshotTest, RoundTripsHeapOwningComponents) {
    const EntityId named = m_entity_manager.createEntity(C_Name{"the player"});
    const EntityId emptyName = m_entity_manager.createEntity(C_Name{""});

    C_WidgetLabel label{};
    label.text_ = "Frames/sec";
    label.colorOverride_ = IRMath::Color{9, 8, 7, 240};
    const EntityId widget = m_entity_manager.createEntity(label);

    C_WidgetImage image{};
    image.path_ = "data/thumb.png";
    image.texelSize_ = IRMath::ivec2(2, 1);
    image.texels_ = {IRMath::Color{10, 20, 30, 255}, IRMath::Color{40, 50, 60, 255}};
    image.loaded_ = true;
    const EntityId imageWidget = m_entity_manager.createEntity(image);

    C_Skeleton skeleton{};
    skeleton.joints_ = {101, IREntity::kNullEntity, 103};
    skeleton.bindPose_.resize(3);
    skeleton.bindPose_[1].translation_ = IRMath::vec3(1.5f, -2.5f, 3.5f);
    const EntityId rig = m_entity_manager.createEntity(skeleton);

    const std::string path = tempPath("heap_owning");
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + path + "'))"));

    m_entity_manager.destroyAllEntities();
    ASSERT_EQ(m_entity_manager.getLiveEntityCount(), 0u);
    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + path + "'))"));

    ASSERT_TRUE(m_entity_manager.entityExists(named));
    EXPECT_EQ(m_entity_manager.getComponent<C_Name>(named).name_, "the player")
        << "C_Name did not survive the Lua round trip";
    ASSERT_TRUE(m_entity_manager.entityExists(emptyName));
    EXPECT_EQ(m_entity_manager.getComponent<C_Name>(emptyName).name_, "");

    ASSERT_TRUE(m_entity_manager.entityExists(widget));
    const C_WidgetLabel &reloadedLabel = m_entity_manager.getComponent<C_WidgetLabel>(widget);
    EXPECT_EQ(reloadedLabel.text_, "Frames/sec");
    EXPECT_EQ(reloadedLabel.colorOverride_.red_, 9);
    EXPECT_EQ(reloadedLabel.colorOverride_.alpha_, 240);

    ASSERT_TRUE(m_entity_manager.entityExists(imageWidget));
    const C_WidgetImage &reloadedImage = m_entity_manager.getComponent<C_WidgetImage>(imageWidget);
    EXPECT_EQ(reloadedImage.path_, "data/thumb.png");
    EXPECT_EQ(reloadedImage.texelSize_, IRMath::ivec2(2, 1));
    ASSERT_EQ(reloadedImage.texels_.size(), 2u);
    EXPECT_EQ(reloadedImage.texels_[1].blue_, 60);
    EXPECT_TRUE(reloadedImage.loaded_);

    ASSERT_TRUE(m_entity_manager.entityExists(rig));
    const C_Skeleton &reloadedRig = m_entity_manager.getComponent<C_Skeleton>(rig);
    ASSERT_EQ(reloadedRig.joints_.size(), 3u);
    EXPECT_EQ(reloadedRig.joints_[0], 101u);
    EXPECT_EQ(reloadedRig.joints_[1], IREntity::kNullEntity)
        << "a severance hole must not be compacted away — slot order is the bone-id space";
    EXPECT_EQ(reloadedRig.joints_[2], 103u);
    ASSERT_EQ(reloadedRig.bindPose_.size(), 3u);
    EXPECT_FLOAT_EQ(reloadedRig.bindPose_[1].translation_.y, -2.5f);
}

// C_GotoEasing3D and C_RotationTarget store the IREasingFunctions enum
// instead of a resolved GLMEasingFunction, so the authored curve survives a
// save/load. Both use non-default curves — kLinearInterpolation is the
// constructor default, so a reader that substituted it would fail here.
//
// The save/load is only half the claim: a reload that restores the right pose
// but stops easing would still pass a bytes-only comparison. The pipeline is
// registered after the reload and ticked once, so the restored components have
// to drive C_LocalTransform through their own curves.
TEST_F(LuaWorldSnapshotTest, RoundTripsEasingComponentsAndKeepsTheCurveLive) {
    const IRMath::vec3 start{1.0f, 2.0f, 3.0f};
    const IRMath::vec3 end{11.0f, -8.0f, 5.0f};
    const IRMath::vec3 axis{0.0f, 1.0f, 0.0f};
    constexpr int kAdvancedFrame = 10;

    C_GotoEasing3D gotoComp{start, end, 2.0f, IRMath::kBounceEaseOut};
    gotoComp.currentFrame_ = kAdvancedFrame; // mid-flight, not freshly authored
    const int durationFrames = gotoComp.durationFrames_;
    ASSERT_GT(durationFrames, kAdvancedFrame + 1) << "test needs a tween still in flight";
    const EntityId easing = m_entity_manager.createEntity(C_LocalTransform{}, gotoComp);

    const EntityId rotating = m_entity_manager.createEntity(
        C_LocalTransform{},
        C_RotationTarget{axis, 0.0f, IRMath::kHalfPi, 0.25f, 0.0f, 1.0f, IRMath::kQuadraticEaseIn}
    );

    const std::string path = tempPath("easing");
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + path + "'))"));

    m_entity_manager.destroyAllEntities();
    ASSERT_EQ(m_entity_manager.getLiveEntityCount(), 0u);
    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + path + "'))"));

    ASSERT_TRUE(m_entity_manager.entityExists(easing));
    const C_GotoEasing3D &reloadedGoto = m_entity_manager.getComponent<C_GotoEasing3D>(easing);
    EXPECT_EQ(reloadedGoto.easingFunction_, IRMath::kBounceEaseOut)
        << "the authored easing curve did not survive the round trip";
    EXPECT_EQ(reloadedGoto.currentFrame_, kAdvancedFrame) << "mid-flight tween state was reset";
    EXPECT_EQ(reloadedGoto.durationFrames_, durationFrames);
    EXPECT_FALSE(reloadedGoto.done_);
    EXPECT_FLOAT_EQ(reloadedGoto.startPos_.x, start.x);
    EXPECT_FLOAT_EQ(reloadedGoto.endPos_.y, end.y);

    ASSERT_TRUE(m_entity_manager.entityExists(rotating));
    const C_RotationTarget &reloadedTarget =
        m_entity_manager.getComponent<C_RotationTarget>(rotating);
    EXPECT_EQ(reloadedTarget.easingFunction_, IRMath::kQuadraticEaseIn)
        << "the authored response curve did not survive the round trip";
    EXPECT_FLOAT_EQ(reloadedTarget.input_, 0.25f);
    EXPECT_FLOAT_EQ(reloadedTarget.maxAngle_, IRMath::kHalfPi);

    // Registered after the reload: clearing the world takes the system
    // entities with it.
    m_system_manager.registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::GOTO_3D>(),
         IRSystem::createSystem<IRSystem::ROTATION_TARGET_LOCAL_TRANSFORM>()}
    );
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    const int tickedFrame = kAdvancedFrame + 1;
    EXPECT_EQ(m_entity_manager.getComponent<C_GotoEasing3D>(easing).currentFrame_, tickedFrame);
    const IRMath::vec3 expectedTranslation = IRMath::mix(
        start,
        end,
        IRMath::kEasingFunctions.at(IRMath::kBounceEaseOut)(
            static_cast<float>(tickedFrame) / static_cast<float>(durationFrames)
        )
    );
    const IRMath::vec3 translation =
        m_entity_manager.getComponent<C_LocalTransform>(easing).translation_;
    EXPECT_NEAR(translation.x, expectedTranslation.x, 1e-4f);
    EXPECT_NEAR(translation.y, expectedTranslation.y, 1e-4f);
    EXPECT_NEAR(translation.z, expectedTranslation.z, 1e-4f);

    // quadraticEaseIn(0.25) = 0.0625, mapped onto [0, kHalfPi].
    const IRMath::vec4 expectedRotation = IRMath::quatAxisAngle(axis, IRMath::kHalfPi * 0.0625f);
    const IRMath::vec4 rotation =
        m_entity_manager.getComponent<C_LocalTransform>(rotating).rotation_;
    // q and -q are the same rotation, so compare by action on the basis vectors.
    for (const auto &v : {IRMath::vec3(1, 0, 0), IRMath::vec3(0, 1, 0), IRMath::vec3(0, 0, 1)}) {
        const IRMath::vec3 actual = IRMath::rotateVectorByQuat(v, rotation);
        const IRMath::vec3 expected = IRMath::rotateVectorByQuat(v, expectedRotation);
        EXPECT_NEAR(actual.x, expected.x, 1e-4f);
        EXPECT_NEAR(actual.y, expected.y, 1e-4f);
        EXPECT_NEAR(actual.z, expected.z, 1e-4f);
    }
}

// C_AngularVelocity is a spin caught mid-decay: every field differs from the
// constructor default, so a reload that substituted one fails here. One tick
// after the reload shows the restored rate and damping still drive the
// transform, not only that the bytes came back.
// Every authored C_CollisionResponse field survives the raw-image round trip;
// `resting_` is recomputed by the next COLLISION_RESPONSE tick.
TEST_F(LuaWorldSnapshotTest, RoundTripsCollisionResponse) {
    C_CollisionResponse authored{0.4f, 0.3f, false, -7.25f};
    authored.restSpeed_ = 3.5f;
    const EntityId body = m_entity_manager.createEntity(C_LocalTransform{}, authored);

    const std::string path = tempPath("collision_response");
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + path + "'))"));

    m_entity_manager.destroyAllEntities();
    ASSERT_EQ(m_entity_manager.getLiveEntityCount(), 0u);
    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + path + "'))"));

    ASSERT_TRUE(m_entity_manager.entityExists(body));
    const C_CollisionResponse &reloaded = m_entity_manager.getComponent<C_CollisionResponse>(body);
    EXPECT_EQ(reloaded.restitution_, authored.restitution_);
    EXPECT_EQ(reloaded.friction_, authored.friction_);
    EXPECT_EQ(reloaded.restSpeed_, authored.restSpeed_);
    EXPECT_EQ(reloaded.floorZ_, authored.floorZ_);
    EXPECT_TRUE(reloaded.hasFloor_);
    EXPECT_FALSE(reloaded.restOnSupport_);
}

TEST_F(LuaWorldSnapshotTest, RoundTripsAngularVelocityAndKeepsTheDecayLive) {
    const IRMath::vec3 axis{0.0f, 0.6f, 0.8f};
    constexpr float kRate = 0.25f;
    constexpr float kDamping = 0.2f;

    const EntityId spinning =
        m_entity_manager.createEntity(C_LocalTransform{}, C_AngularVelocity{axis, kRate, kDamping});

    const std::string path = tempPath("angular_velocity");
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + path + "'))"));

    m_entity_manager.destroyAllEntities();
    ASSERT_EQ(m_entity_manager.getLiveEntityCount(), 0u);
    ASSERT_TRUE(runOk("assert(IRPersist.loadWorld('" + path + "'))"));

    ASSERT_TRUE(m_entity_manager.entityExists(spinning));
    const C_AngularVelocity &reloaded = m_entity_manager.getComponent<C_AngularVelocity>(spinning);
    EXPECT_FLOAT_EQ(reloaded.axis_.x, axis.x);
    EXPECT_FLOAT_EQ(reloaded.axis_.y, axis.y);
    EXPECT_FLOAT_EQ(reloaded.axis_.z, axis.z);
    EXPECT_FLOAT_EQ(reloaded.radiansPerFrame_, kRate);
    EXPECT_FLOAT_EQ(reloaded.dampingPerFrame_, kDamping);

    // Registered after the reload: clearing the world takes the system
    // entities with it.
    m_system_manager.registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::ANGULAR_VELOCITY_DAMPED>()}
    );
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    EXPECT_FLOAT_EQ(
        m_entity_manager.getComponent<C_AngularVelocity>(spinning).radiansPerFrame_,
        kRate * (1.0f - kDamping)
    );
    const IRMath::vec4 expectedRotation = IRMath::quatAxisAngle(axis, kRate);
    const IRMath::vec4 rotation =
        m_entity_manager.getComponent<C_LocalTransform>(spinning).rotation_;
    // q and -q are the same rotation, so compare by action on the basis vectors.
    for (const auto &v : {IRMath::vec3(1, 0, 0), IRMath::vec3(0, 1, 0), IRMath::vec3(0, 0, 1)}) {
        const IRMath::vec3 actual = IRMath::rotateVectorByQuat(v, rotation);
        const IRMath::vec3 expected = IRMath::rotateVectorByQuat(v, expectedRotation);
        EXPECT_NEAR(actual.x, expected.x, 1e-4f);
        EXPECT_NEAR(actual.y, expected.y, 1e-4f);
        EXPECT_NEAR(actual.z, expected.z, 1e-4f);
    }
}

// W-8 determinism over the widened registry: a world carrying heap-owning
// components must still double-save byte-identically. The map-ordering hazard
// this guards against (C_BindPoints) is unit-tested directly; this is the
// end-to-end backstop through the real save path.
TEST_F(LuaWorldSnapshotTest, DoubleSaveIsByteIdenticalWithHeapOwningComponents) {
    m_entity_manager.createEntity(C_Name{"alpha"});
    m_entity_manager.createEntity(C_Name{"beta"}, C_PositionInt3D{1, 2, 3});

    C_BindPoints points{};
    points.setPoint(
        "hand.R",
        IRComponents::BindPointRuntime{
            2,
            IRMath::vec3(1.0f, 0.0f, 0.0f),
            IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f)
        }
    );
    points.setPoint(
        "hand.L",
        IRComponents::BindPointRuntime{
            1,
            IRMath::vec3(-1.0f, 0.0f, 0.0f),
            IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f)
        }
    );
    points.setPoint(
        "head",
        IRComponents::BindPointRuntime{
            0,
            IRMath::vec3(0.0f, 0.0f, 2.0f),
            IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f)
        }
    );
    m_entity_manager.createEntity(points);

    // The enum-stored easing components carry tail padding in their raw byte
    // image, so they belong in the determinism backstop too.
    m_entity_manager.createEntity(
        C_LocalTransform{},
        C_GotoEasing3D{IRMath::vec3{0.0f}, IRMath::vec3{1.0f}, 1.0f, IRMath::kCubicEaseInOut}
    );
    m_entity_manager.createEntity(
        C_LocalTransform{},
        C_RotationTarget{
            IRMath::vec3{0.0f, 0.0f, 1.0f},
            0.0f,
            IRMath::kHalfPi,
            0.5f,
            0.0f,
            1.0f,
            IRMath::kSineEaseOut
        }
    );

    const std::string first = tempPath("determinism_a");
    const std::string second = tempPath("determinism_b");
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + first + "'))"));
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + second + "'))"));

    EXPECT_EQ(readFileBytes(first), readFileBytes(second))
        << "same-world double save is not byte-identical";
}

// Missing/corrupt path -> false with no Lua error (I/O failure is a bool, not
// an exception). evalTrue would report failure if the call raised, so this
// covers both halves of the contract. (Zero *gameplay* mutation on a failed
// load is the world layer's own guarantee, exercised by world_snapshot_test;
// here loadWorld still mints the default registry's component-backing entities,
// which is not saved-content mutation.)
TEST_F(LuaWorldSnapshotTest, MissingPathReturnsFalseNoThrow) {
    EXPECT_TRUE(evalTrue("IRPersist.loadWorld('" + tempPath("does_not_exist") + "') == false"));
}

// A non-string argument is a misuse -> Lua error (sol coercion), not a silent
// false — mirrors the IRSave binding's I/O-fails->bool / misuse->error split.
TEST_F(LuaWorldSnapshotTest, NonStringArgRaises) {
    EXPECT_TRUE(raisesError("IRPersist.saveWorld({})"));
    EXPECT_TRUE(raisesError("IRPersist.loadWorld({})"));
}

// W-11: with IR_PERSIST_DUMP set, saveWorld emits a `<path>.json.txt` detailed
// dump containing a known entity id + the CHILD_OF relation name; with the flag
// unset the dump is absent AND the binary is byte-identical (side-output only).
TEST_F(LuaWorldSnapshotTest, DumpModeAndByteParity) {
    const EntityId parent =
        m_entity_manager.createEntity(C_LocalTransform{IRMath::vec3(1.0f, 1.0f, 1.0f)});
    const EntityId child = m_entity_manager.createEntity(C_PositionInt3D{2, 3, 4});
    IREntity::setParent(child, parent);
    const EntityId maskedChild = child & IREntity::IR_ENTITY_ID_BITS;

    const std::string dumpPath = tempPath("dump_on");
    const std::string plainPath = tempPath("dump_off");

    setDumpFlag(true);
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + dumpPath + "'))"));
    setDumpFlag(false);
    ASSERT_TRUE(runOk("assert(IRPersist.saveWorld('" + plainPath + "'))"));

    // Flag-off byte parity: the binary is identical regardless of the dump.
    EXPECT_EQ(readFileBytes(dumpPath), readFileBytes(plainPath));

    // Flag-on produced the dump; flag-off did not.
    ASSERT_TRUE(fileExists(dumpPath + ".json.txt"));
    EXPECT_FALSE(fileExists(plainPath + ".json.txt"));

    const std::vector<std::uint8_t> dumpBytes = readFileBytes(dumpPath + ".json.txt");
    const std::string dump(dumpBytes.begin(), dumpBytes.end());
    ASSERT_FALSE(dump.empty());
    EXPECT_EQ(dump.front(), '{'); // JSON object shape
    EXPECT_NE(dump.find("\"format\""), std::string::npos);
    EXPECT_NE(dump.find("CHILD_OF"), std::string::npos);                  // relation name
    EXPECT_NE(dump.find(std::to_string(maskedChild)), std::string::npos); // known id
}

} // namespace
