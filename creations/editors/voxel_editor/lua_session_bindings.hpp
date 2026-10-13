#ifndef IR_VOXEL_EDITOR_LUA_SESSION_BINDINGS_H
#define IR_VOXEL_EDITOR_LUA_SESSION_BINDINGS_H

#include "component_records.hpp"
#include "session_builder.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace IRVoxelEditor::LuaSession {

struct Lifetime {
    Session::Builder *builder_ = nullptr;
    IRScript::LuaScript *script_ = nullptr;
};

inline sol::error argumentError(const char *method, const char *argument, const char *expected) {
    return sol::error{
        std::string("IREditor session ") + method + ": argument '" + argument + "' must be " +
        expected
    };
}

inline sol::table tableArg(const sol::object &value, const char *method, const char *argument) {
    if (value.get_type() != sol::type::table)
        throw argumentError(method, argument, "a table");
    return value.as<sol::table>();
}

inline float numberArg(const sol::object &value, const char *method, const char *argument) {
    const sol::optional<float> parsed = value.as<sol::optional<float>>();
    if (!parsed || !IRMath::isFinite(*parsed))
        throw argumentError(method, argument, "a finite number");
    return *parsed;
}

inline std::string stringArg(const sol::object &value, const char *method, const char *argument) {
    if (value.get_type() != sol::type::string)
        throw argumentError(method, argument, "a string");
    return value.as<std::string>();
}

inline bool validName(const std::string &name) {
    if (name.empty() || name.front() < 'a' || name.front() > 'z')
        return false;
    return std::all_of(name.begin() + 1, name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

inline int integerArg(const sol::object &value, const char *method, const char *argument) {
    const float parsed = numberArg(value, method, argument);
    if (static_cast<double>(parsed) < static_cast<double>(std::numeric_limits<int>::min()) ||
        static_cast<double>(parsed) > static_cast<double>(std::numeric_limits<int>::max())) {
        throw argumentError(method, argument, "an integer");
    }
    const int integer = static_cast<int>(parsed);
    if (static_cast<float>(integer) != parsed)
        throw argumentError(method, argument, "an integer");
    return integer;
}

inline sol::object vectorEntry(const sol::table &table, const char *key, int index) {
    const sol::object keyed = table[key];
    return keyed.valid() && keyed.get_type() != sol::type::lua_nil ? keyed
                                                                   : sol::object(table[index]);
}

inline IRMath::ivec2 ivec2Arg(const sol::object &value, const char *method, const char *argument) {
    const sol::table table = tableArg(value, method, argument);
    return IRMath::ivec2{
        integerArg(vectorEntry(table, "x", 1), method, argument),
        integerArg(vectorEntry(table, "y", 2), method, argument)
    };
}

inline IRMath::ivec3 ivec3Arg(const sol::object &value, const char *method, const char *argument) {
    const sol::table table = tableArg(value, method, argument);
    return IRMath::ivec3{
        integerArg(vectorEntry(table, "x", 1), method, argument),
        integerArg(vectorEntry(table, "y", 2), method, argument),
        integerArg(vectorEntry(table, "z", 3), method, argument)
    };
}

inline IRMath::vec2 vec2Arg(const sol::object &value, const char *method, const char *argument) {
    const sol::table table = tableArg(value, method, argument);
    return IRMath::vec2{
        numberArg(vectorEntry(table, "x", 1), method, argument),
        numberArg(vectorEntry(table, "y", 2), method, argument)
    };
}

inline IRMath::vec3 vec3Arg(const sol::object &value, const char *method, const char *argument) {
    const sol::table table = tableArg(value, method, argument);
    return IRMath::vec3{
        numberArg(vectorEntry(table, "x", 1), method, argument),
        numberArg(vectorEntry(table, "y", 2), method, argument),
        numberArg(vectorEntry(table, "z", 3), method, argument)
    };
}

inline IRMath::vec4 vec4Arg(const sol::object &value, const char *method, const char *argument) {
    const sol::table table = tableArg(value, method, argument);
    return IRMath::vec4{
        numberArg(vectorEntry(table, "x", 1), method, argument),
        numberArg(vectorEntry(table, "y", 2), method, argument),
        numberArg(vectorEntry(table, "z", 3), method, argument),
        numberArg(vectorEntry(table, "w", 4), method, argument)
    };
}

inline IRVoxelEditor::SliderGeometry
sliderGeometryArg(const sol::object &value, const char *method, const char *argument) {
    const sol::table table = tableArg(value, method, argument);
    return IRVoxelEditor::SliderGeometry{
        ivec2Arg(table["pos"], method, "geometry.pos"),
        ivec2Arg(table["size"], method, "geometry.size")
    };
}

inline IRInput::KeyMouseButtons
keyArg(const sol::object &value, const char *method, const char *argument) {
    const int key = integerArg(value, method, argument);
    if (key <= static_cast<int>(IRInput::kNullButton) ||
        key >= static_cast<int>(IRInput::kNumKeyMouseButtons)) {
        throw argumentError(method, argument, "an IRInput.Key value");
    }
    return static_cast<IRInput::KeyMouseButtons>(key);
}

inline Session::SliderTarget
sliderTargetArg(const sol::object &value, const char *method, const char *argument) {
    const int target = integerArg(value, method, argument);
    switch (target) {
    case static_cast<int>(Session::SliderTarget::FPS):
        return Session::SliderTarget::FPS;
    case static_cast<int>(Session::SliderTarget::SCRUBBER):
        return Session::SliderTarget::SCRUBBER;
    case static_cast<int>(Session::SliderTarget::LOD_FINE):
        return Session::SliderTarget::LOD_FINE;
    case static_cast<int>(Session::SliderTarget::LOD_COARSE):
        return Session::SliderTarget::LOD_COARSE;
    case static_cast<int>(Session::SliderTarget::LOD_TIER):
        return Session::SliderTarget::LOD_TIER;
    default:
        throw argumentError(method, argument, "an IREditor.SliderTarget value");
    }
}

inline Session::EditorInputCheckKind
editorInputKindArg(const sol::object &value, const char *method, const char *argument) {
    const int kind = integerArg(value, method, argument);
    switch (kind) {
    case static_cast<int>(Session::EditorInputCheckKind::SELECTED_PART_NO_TEXT_FOCUS):
        return Session::EditorInputCheckKind::SELECTED_PART_NO_TEXT_FOCUS;
    case static_cast<int>(Session::EditorInputCheckKind::TEXT_CAPTURED_X):
        return Session::EditorInputCheckKind::TEXT_CAPTURED_X;
    case static_cast<int>(Session::EditorInputCheckKind::TEXT_FOCUS_RELEASED):
        return Session::EditorInputCheckKind::TEXT_FOCUS_RELEASED;
    case static_cast<int>(Session::EditorInputCheckKind::X_SYMMETRY_ENABLED):
        return Session::EditorInputCheckKind::X_SYMMETRY_ENABLED;
    case static_cast<int>(Session::EditorInputCheckKind::TAB_WALK_COVERAGE):
        return Session::EditorInputCheckKind::TAB_WALK_COVERAGE;
    default:
        throw argumentError(method, argument, "an IREditor.EditorInputCheckKind value");
    }
}

inline Session::LoftMask
loftMaskArg(const sol::object &value, const char *method, const char *argument) {
    const int mask = integerArg(value, method, argument);
    switch (mask) {
    case static_cast<int>(Session::LoftMask::XZ):
        return Session::LoftMask::XZ;
    case static_cast<int>(Session::LoftMask::YZ):
        return Session::LoftMask::YZ;
    default:
        throw argumentError(method, argument, "an IREditor.LoftMask value");
    }
}

template <typename T>
inline T enumArg(
    const sol::object &value,
    const char *method,
    const char *argument,
    T first,
    T last,
    const char *expected
) {
    const int parsed = integerArg(value, method, argument);
    if (parsed < static_cast<int>(first) || parsed > static_cast<int>(last)) {
        throw argumentError(method, argument, expected);
    }
    return static_cast<T>(parsed);
}

inline IRComponents::RotationMode
rotationModeArg(const sol::object &value, const char *method, const char *argument) {
    return enumArg(
        value,
        method,
        argument,
        IRComponents::RotationMode::kFirst,
        IRComponents::RotationMode::kLast,
        "an IRComponent.RotationMode value"
    );
}

inline IRComponents::EntityAnchor
entityAnchorArg(const sol::object &value, const char *method, const char *argument) {
    return enumArg(
        value,
        method,
        argument,
        IRComponents::EntityAnchor::kFirst,
        IRComponents::EntityAnchor::kLast,
        "an IRComponent.EntityAnchor value"
    );
}

class Handle {
  public:
    explicit Handle(std::shared_ptr<Lifetime> lifetime)
        : m_lifetime(std::move(lifetime)) {}

    sol::table sceneSize() const {
        const IRMath::ivec3 value = builder().sceneSize();
        return script().lua().create_table_with("x", value.x, "y", value.y, "z", value.z);
    }

    sol::table sceneOrigin() const {
        const IRMath::vec3 value = builder().sceneOrigin();
        return script().lua().create_table_with("x", value.x, "y", value.y, "z", value.z);
    }

    void withReferenceFurniture() {
        builder().withReferenceFurniture();
    }
    void segment(const sol::object &label, sol::optional<sol::object> zoom) {
        const std::string parsedLabel = stringArg(label, "segment", "label");
        if (!validName(parsedLabel))
            throw argumentError("segment", "label", "a name matching [a-z][a-z0-9_]*");
        const float parsedZoom = zoom ? numberArg(*zoom, "segment", "zoom") : Session::kSessionZoom;
        builder().segment(parsedLabel.c_str(), parsedZoom);
    }
    void click(const sol::object &target) {
        builder().click(ivec3Arg(target, "click", "target"));
    }
    void clickExpectingNoEdit(const sol::object &target) {
        builder().clickExpectingNoEdit(ivec3Arg(target, "clickExpectingNoEdit", "target"));
    }
    void clickFace(const sol::object &anchor, const sol::object &normal) {
        builder().clickFace(
            ivec3Arg(anchor, "clickFace", "anchor"),
            ivec3Arg(normal, "clickFace", "normal")
        );
    }
    void clickBelow(const sol::object &target) {
        builder().clickBelow(ivec3Arg(target, "clickBelow", "target"));
    }
    void hover(const sol::object &target) {
        builder().hover(ivec3Arg(target, "hover", "target"));
    }
    void dragBox(const sol::object &a, const sol::object &b) {
        builder().dragBox(ivec3Arg(a, "dragBox", "a"), ivec3Arg(b, "dragBox", "b"));
    }
    void tapKey(const sol::object &key) {
        builder().tapKey(keyArg(key, "tapKey", "key"));
    }
    void chordKey(const sol::object &modifier, const sol::object &key) {
        builder().chordKey(
            keyArg(modifier, "chordKey", "modifier"),
            keyArg(key, "chordKey", "key")
        );
    }

#define IR_SESSION_FORWARD_0(name)                                                                 \
    void name() {                                                                                  \
        builder().name();                                                                          \
    }
    IR_SESSION_FORWARD_0(toggleEraseMode)
    IR_SESSION_FORWARD_0(addLayer)
    IR_SESSION_FORWARD_0(selectPrevLayer)
    IR_SESSION_FORWARD_0(selectNextLayer)
    IR_SESSION_FORWARD_0(toggleActiveLayerVisibility)
    IR_SESSION_FORWARD_0(duplicateFrame)
    IR_SESSION_FORWARD_0(prevFrame)
    IR_SESSION_FORWARD_0(nextFrame)
    IR_SESSION_FORWARD_0(save)
    IR_SESSION_FORWARD_0(reload)
    IR_SESSION_FORWARD_0(addVoxelPart)
    IR_SESSION_FORWARD_0(nextPart)
    IR_SESSION_FORWARD_0(toggleLoftMode)
    IR_SESSION_FORWARD_0(stampLoft)
    IR_SESSION_FORWARD_0(removeSelectedPart)
    IR_SESSION_FORWARD_0(removeSelectedPartWithButton)
    IR_SESSION_FORWARD_0(undoRemovedPart)
    IR_SESSION_FORWARD_0(discardRemovedPart)
    IR_SESSION_FORWARD_0(toggleLodFollowZoom)
    IR_SESSION_FORWARD_0(clearEntityScene)
    IR_SESSION_FORWARD_0(toggleRotationalSymmetry)
#undef IR_SESSION_FORWARD_0

    void enableSymmetry(bool x, bool y, bool z) {
        builder().enableSymmetry(x, y, z);
    }
    void selectPaletteSwatch(int index) {
        builder().selectPaletteSwatch(index);
    }
    void clickGui(const sol::object &point) {
        builder().clickGui(vec2Arg(point, "clickGui", "point"));
    }
    void wheelGui(const sol::object &point, int rows) {
        builder().wheelGui(vec2Arg(point, "wheelGui", "point"), rows);
    }
    void wheelScene(const sol::object &target, int rows) {
        builder().wheelScene(ivec3Arg(target, "wheelScene", "target"), rows);
    }
    void selectPartThroughList(int visibleRow, int partIndex) {
        builder().selectPartThroughList(visibleRow, partIndex);
    }
    void typeText(const sol::object &point, const std::string &text) {
        builder().typeText(vec2Arg(point, "typeText", "point"), text);
    }
    void dragGuiSlider(const sol::object &geometry, float minValue, float maxValue, float value) {
        builder().dragGuiSlider(
            sliderGeometryArg(geometry, "dragGuiSlider", "geometry"),
            minValue,
            maxValue,
            value
        );
    }
    void setPartSize(const sol::object &size) {
        builder().setPartSize(ivec3Arg(size, "setPartSize", "size"));
    }
    void bakeSphere(float radius) {
        builder().bakeSphere(radius);
    }
    void clickLoftCell(const sol::object &mask, const sol::object &cell) {
        builder().clickLoftCell(
            loftMaskArg(mask, "clickLoftCell", "mask"),
            ivec2Arg(cell, "clickLoftCell", "cell")
        );
    }
    void dragLodSlider(const sol::object &target, int tier) {
        builder().dragLodSlider(sliderTargetArg(target, "dragLodSlider", "target"), tier);
    }
    void dragWorld(const sol::object &from, const sol::object &to) {
        builder().dragWorld(vec3Arg(from, "dragWorld", "from"), vec3Arg(to, "dragWorld", "to"));
    }
    void
    dragPartGizmo(const sol::object &from, const sol::object &to, const sol::object &partDelta) {
        builder().dragPartGizmo(
            vec3Arg(from, "dragPartGizmo", "from"),
            vec3Arg(to, "dragPartGizmo", "to"),
            vec3Arg(partDelta, "dragPartGizmo", "partDelta")
        );
    }
    void selectPartMode(const sol::object &mode) {
        builder().selectPartMode(rotationModeArg(mode, "selectPartMode", "mode"));
    }
    void expectOccupancy(const sol::object &local, bool occupied, const std::string &name) {
        builder().expectOccupancy(ivec3Arg(local, "expectOccupancy", "local"), occupied, name);
    }
    void expectPartOccupancy(
        int part, const sol::object &local, bool occupied, const std::string &name
    ) {
        builder().expectPartOccupancy(
            part,
            ivec3Arg(local, "expectPartOccupancy", "local"),
            occupied,
            name
        );
    }
    void expectPartTransform(
        int part, const sol::object &expected, float tolerance, bool equal, const std::string &name
    ) {
        builder().expectPartTransform(
            part,
            vec3Arg(expected, "expectPartTransform", "expected"),
            tolerance,
            equal,
            name
        );
    }
    void expectPartCount(int expected, const std::string &name) {
        builder().expectPartCount(expected, name);
    }
    void expectPartId(int part, const std::string &expected, const std::string &name) {
        builder().expectPartId(part, expected, name);
    }
    void expectPartAuthoredCount(int part, int expected, const std::string &name) {
        builder().expectPartAuthoredCount(part, expected, name);
    }
    void expectPartEditable(int part, bool expected, const std::string &name) {
        builder().expectPartEditable(part, expected, name);
    }
    void expectSelectedPartVisible(int part, const std::string &name) {
        builder().expectSelectedPartVisible(part, name);
    }
    void expectPartsListTop(int part, const std::string &name) {
        builder().expectPartsListTop(part, name);
    }
    void expectCameraZoom(float zoom, bool equal, const std::string &name) {
        builder().expectCameraZoom(zoom, equal, name);
    }
    void applyRadialArray(int count) {
        builder().applyRadialArray(count);
    }
    void applyRadialArrayToLiveScene(int count) {
        builder().applyRadialArrayToLiveScene(count);
    }
    void expectCanvasCount(int offset, const std::string &name) {
        builder().expectCanvasCount(offset, name);
    }
    void expectRotationMode(int part, const sol::object &mode, const std::string &name) {
        builder()
            .expectRotationMode(part, rotationModeArg(mode, "expectRotationMode", "mode"), name);
    }
    void expectPartAnchor(int part, const sol::object &expected, const std::string &name) {
        builder().expectPartAnchor(
            part,
            entityAnchorArg(expected, "expectPartAnchor", "expected"),
            name
        );
    }
    void expectPartModeDropdown(const sol::object &expected, const std::string &name) {
        builder().expectPartModeDropdown(
            rotationModeArg(expected, "expectPartModeDropdown", "expected"),
            name
        );
    }
    void expectEditorInput(const sol::object &kind, int expectedPart, const std::string &name) {
        builder().expectEditorInput(
            editorInputKindArg(kind, "expectEditorInput", "kind"),
            expectedPart,
            name
        );
    }
    void expectPoolActive(const sol::object &local, bool active, const std::string &name) {
        builder().expectPoolActive(ivec3Arg(local, "expectPoolActive", "local"), active, name);
    }
    void expectVoxelColor(const sol::object &local, int palette, const std::string &name) {
        builder().expectVoxelColor(ivec3Arg(local, "expectVoxelColor", "local"), palette, name);
    }
    void expectPick(const sol::object &local, const std::string &name) {
        builder().expectPick(ivec3Arg(local, "expectPick", "local"), name);
    }
    void expectPickMatchesRender(const std::string &name) {
        builder().expectPickMatchesRender(name);
    }
    void expectSliderValue(
        const sol::object &target, float expected, float tolerance, const std::string &name
    ) {
        builder().expectSliderValue(
            sliderTargetArg(target, "expectSliderValue", "target"),
            expected,
            tolerance,
            name
        );
    }
    void expectPartGated(int part, bool gated, int tier, const std::string &name) {
        builder().expectPartGated(part, gated, tier, name);
    }
    void expectPreviewGated(bool gated, int tier, const std::string &name) {
        builder().expectPreviewGated(gated, tier, name);
    }
    void expectPartExtent(int part, const sol::object &expected, const std::string &name) {
        builder().expectPartExtent(part, ivec3Arg(expected, "expectPartExtent", "expected"), name);
    }
    void expectMirrorOffsets(const sol::object &offsets, const std::string &name) {
        builder().expectMirrorOffsets(vec3Arg(offsets, "expectMirrorOffsets", "offsets"), name);
    }
    void expectLoftMask(
        const sol::object &mask, const sol::object &cell, bool on, const std::string &name
    ) {
        builder().expectLoftMask(
            loftMaskArg(mask, "expectLoftMask", "mask"),
            ivec2Arg(cell, "expectLoftMask", "cell"),
            on,
            name
        );
    }
    void expectManifestContains(const std::string &text, const std::string &name) {
        builder().expectManifestContains(text, name);
    }
    void expectManifestOmits(const std::string &text, const std::string &name) {
        builder().expectManifestOmits(text, name);
    }
    void
    expectComponentRegistered(const std::string &component, int fields, const std::string &name) {
        builder().expectComponentRegistered(component, fields, name);
    }
    void
    expectPanelLabel(const std::string &panel, const std::string &label, const std::string &name) {
        builder().expectPanelLabel(panel, label, name);
    }
    void expectComponentValue(
        int target,
        const std::string &component,
        const std::string &field,
        const sol::object &expected,
        bool equal,
        const std::string &name
    ) {
        builder().expectComponentValue(
            target,
            component,
            field,
            componentValue(component, field, expected),
            equal,
            name
        );
    }
    void recordError(const std::string &why) {
        builder().recordError(why);
    }

  private:
    Session::Builder &builder() const {
        if (!m_lifetime || m_lifetime->builder_ == nullptr)
            throw sol::error{"session handle used outside its registerSession function"};
        return *m_lifetime->builder_;
    }

    IRScript::LuaScript &script() const {
        builder();
        return *m_lifetime->script_;
    }

    std::optional<ComponentFieldValue> componentValue(
        const std::string &component, const std::string &field, const sol::object &value
    ) const {
        const IRScript::LuaTypedComponentInfo *info =
            detail::findModuleComponent(script(), component);
        if (info == nullptr)
            throw sol::error{
                "IREditor session expectComponentValue: unknown component '" + component + "'"
            };
        const auto found =
            std::find_if(info->fields_.begin(), info->fields_.end(), [&](const auto &f) {
                return f.name_ == field;
            });
        if (found == info->fields_.end())
            throw sol::error{
                "IREditor session expectComponentValue: unknown field '" + field + "'"
            };
        if (found->type_ == IRScript::LuaFieldType::FUNCTION ||
            found->type_ == IRScript::LuaFieldType::TABLE) {
            throw sol::error{
                "IREditor session expectComponentValue: field '" + field + "' is not comparable"
            };
        }
        if (value.get_type() == sol::type::lua_nil)
            return std::nullopt;
        ComponentFieldValue converted;
        switch (found->type_) {
        case IRScript::LuaFieldType::INT32:
            converted =
                static_cast<std::int32_t>(integerArg(value, "expectComponentValue", "expected"));
            break;
        case IRScript::LuaFieldType::FLOAT:
            converted = numberArg(value, "expectComponentValue", "expected");
            break;
        case IRScript::LuaFieldType::BOOL:
            if (!value.is<bool>())
                throw argumentError("expectComponentValue", "expected", "a boolean");
            converted = value.as<bool>();
            break;
        case IRScript::LuaFieldType::STRING:
            if (!value.is<std::string>())
                throw argumentError("expectComponentValue", "expected", "a string");
            converted = value.as<std::string>();
            break;
        case IRScript::LuaFieldType::VEC3:
            converted = vec3Arg(value, "expectComponentValue", "expected");
            break;
        case IRScript::LuaFieldType::IVEC3:
            converted = ivec3Arg(value, "expectComponentValue", "expected");
            break;
        case IRScript::LuaFieldType::VEC4:
            converted = vec4Arg(value, "expectComponentValue", "expected");
            break;
        case IRScript::LuaFieldType::FUNCTION:
        case IRScript::LuaFieldType::TABLE:
            break;
        }
        if (std::holds_alternative<std::monostate>(converted))
            throw argumentError(
                "expectComponentValue",
                "expected",
                "a value matching the field type"
            );
        return converted;
    }

    std::shared_ptr<Lifetime> m_lifetime;
};

inline void bind(IRScript::LuaScript &script, sol::table editor) {
    script.bindLuaInputEnums();
    sol::state &lua = script.lua();
    lua.new_usertype<Handle>(
        "_IRVoxelEditorSession",
        sol::no_constructor,
        "sceneSize",
        &Handle::sceneSize,
        "sceneOrigin",
        &Handle::sceneOrigin,
        "withReferenceFurniture",
        &Handle::withReferenceFurniture,
        "segment",
        &Handle::segment,
        "click",
        &Handle::click,
        "clickExpectingNoEdit",
        &Handle::clickExpectingNoEdit,
        "clickFace",
        &Handle::clickFace,
        "clickBelow",
        &Handle::clickBelow,
        "hover",
        &Handle::hover,
        "dragBox",
        &Handle::dragBox,
        "tapKey",
        &Handle::tapKey,
        "chordKey",
        &Handle::chordKey,
        "toggleEraseMode",
        &Handle::toggleEraseMode,
        "enableSymmetry",
        &Handle::enableSymmetry,
        "addLayer",
        &Handle::addLayer,
        "selectPrevLayer",
        &Handle::selectPrevLayer,
        "selectNextLayer",
        &Handle::selectNextLayer,
        "toggleActiveLayerVisibility",
        &Handle::toggleActiveLayerVisibility,
        "selectPaletteSwatch",
        &Handle::selectPaletteSwatch,
        "clickGui",
        &Handle::clickGui,
        "wheelGui",
        &Handle::wheelGui,
        "wheelScene",
        &Handle::wheelScene,
        "selectPartThroughList",
        &Handle::selectPartThroughList,
        "typeText",
        &Handle::typeText,
        "dragGuiSlider",
        &Handle::dragGuiSlider,
        "duplicateFrame",
        &Handle::duplicateFrame,
        "prevFrame",
        &Handle::prevFrame,
        "nextFrame",
        &Handle::nextFrame,
        "save",
        &Handle::save,
        "reload",
        &Handle::reload,
        "setPartSize",
        &Handle::setPartSize,
        "addVoxelPart",
        &Handle::addVoxelPart,
        "bakeSphere",
        &Handle::bakeSphere,
        "nextPart",
        &Handle::nextPart,
        "toggleLoftMode",
        &Handle::toggleLoftMode,
        "clickLoftCell",
        &Handle::clickLoftCell,
        "stampLoft",
        &Handle::stampLoft,
        "removeSelectedPart",
        &Handle::removeSelectedPart,
        "removeSelectedPartWithButton",
        &Handle::removeSelectedPartWithButton,
        "undoRemovedPart",
        &Handle::undoRemovedPart,
        "discardRemovedPart",
        &Handle::discardRemovedPart,
        "dragLodSlider",
        &Handle::dragLodSlider,
        "toggleLodFollowZoom",
        &Handle::toggleLodFollowZoom,
        "clearEntityScene",
        &Handle::clearEntityScene,
        "dragWorld",
        &Handle::dragWorld,
        "dragPartGizmo",
        &Handle::dragPartGizmo,
        "selectPartMode",
        &Handle::selectPartMode,
        "applyRadialArray",
        &Handle::applyRadialArray,
        "applyRadialArrayToLiveScene",
        &Handle::applyRadialArrayToLiveScene,
        "toggleRotationalSymmetry",
        &Handle::toggleRotationalSymmetry,
        "expectOccupancy",
        &Handle::expectOccupancy,
        "expectPartOccupancy",
        &Handle::expectPartOccupancy,
        "expectPartTransform",
        &Handle::expectPartTransform,
        "expectPartCount",
        &Handle::expectPartCount,
        "expectPartId",
        &Handle::expectPartId,
        "expectPartAuthoredCount",
        &Handle::expectPartAuthoredCount,
        "expectPartEditable",
        &Handle::expectPartEditable,
        "expectSelectedPartVisible",
        &Handle::expectSelectedPartVisible,
        "expectPartsListTop",
        &Handle::expectPartsListTop,
        "expectCameraZoom",
        &Handle::expectCameraZoom,
        "expectCanvasCount",
        &Handle::expectCanvasCount,
        "expectRotationMode",
        &Handle::expectRotationMode,
        "expectPartAnchor",
        &Handle::expectPartAnchor,
        "expectPartModeDropdown",
        &Handle::expectPartModeDropdown,
        "expectEditorInput",
        &Handle::expectEditorInput,
        "expectPoolActive",
        &Handle::expectPoolActive,
        "expectVoxelColor",
        &Handle::expectVoxelColor,
        "expectPick",
        &Handle::expectPick,
        "expectPickMatchesRender",
        &Handle::expectPickMatchesRender,
        "expectSliderValue",
        &Handle::expectSliderValue,
        "expectPartGated",
        &Handle::expectPartGated,
        "expectPreviewGated",
        &Handle::expectPreviewGated,
        "expectPartExtent",
        &Handle::expectPartExtent,
        "expectMirrorOffsets",
        &Handle::expectMirrorOffsets,
        "expectLoftMask",
        &Handle::expectLoftMask,
        "expectManifestContains",
        &Handle::expectManifestContains,
        "expectManifestOmits",
        &Handle::expectManifestOmits,
        "expectComponentRegistered",
        &Handle::expectComponentRegistered,
        "expectPanelLabel",
        &Handle::expectPanelLabel,
        "expectComponentValue",
        &Handle::expectComponentValue,
        "recordError",
        &Handle::recordError
    );

    sol::table sliderTarget = lua.create_table();
#define IR_BIND_SESSION_SLIDER(name)                                                               \
    sliderTarget[#name] = static_cast<lua_Integer>(Session::SliderTarget::name)
    IR_BIND_SESSION_SLIDER(FPS);
    IR_BIND_SESSION_SLIDER(SCRUBBER);
    IR_BIND_SESSION_SLIDER(LOD_FINE);
    IR_BIND_SESSION_SLIDER(LOD_COARSE);
    IR_BIND_SESSION_SLIDER(LOD_TIER);
#undef IR_BIND_SESSION_SLIDER
    editor["SliderTarget"] = sliderTarget;

    sol::table inputCheck = lua.create_table();
#define IR_BIND_SESSION_INPUT_CHECK(name)                                                          \
    inputCheck[#name] = static_cast<lua_Integer>(Session::EditorInputCheckKind::name)
    IR_BIND_SESSION_INPUT_CHECK(SELECTED_PART_NO_TEXT_FOCUS);
    IR_BIND_SESSION_INPUT_CHECK(TEXT_CAPTURED_X);
    IR_BIND_SESSION_INPUT_CHECK(TEXT_FOCUS_RELEASED);
    IR_BIND_SESSION_INPUT_CHECK(X_SYMMETRY_ENABLED);
    IR_BIND_SESSION_INPUT_CHECK(TAB_WALK_COVERAGE);
#undef IR_BIND_SESSION_INPUT_CHECK
    editor["EditorInputCheckKind"] = inputCheck;

    sol::table loftMask = lua.create_table();
#define IR_BIND_SESSION_LOFT_MASK(name)                                                            \
    loftMask[#name] = static_cast<lua_Integer>(Session::LoftMask::name)
    IR_BIND_SESSION_LOFT_MASK(XZ);
    IR_BIND_SESSION_LOFT_MASK(YZ);
#undef IR_BIND_SESSION_LOFT_MASK
    editor["LoftMask"] = loftMask;
}

} // namespace IRVoxelEditor::LuaSession

#endif /* IR_VOXEL_EDITOR_LUA_SESSION_BINDINGS_H */
