#include <irreden/ir_profile.hpp>

#include <irreden/script/lua_script.hpp>

namespace IRScript {

// lua_dofile runs a lua script. Global functions and variables
// can be accessed via the lua stack.
LuaScript::LuaScript()
    : m_lua{} {
    m_lua.open_libraries(
        sol::lib::base,
        sol::lib::package,
        sol::lib::string,
        sol::lib::table,
        sol::lib::math
    );

    // Engine-provided utility functions that are available to all Lua creations.
    m_lua["IRMath"] = m_lua.create_table();
    m_lua["IRMath"]["fract"] = [](float value) { return IRMath::fract(value); };
    m_lua["IRMath"]["clamp01"] = [](float value) { return IRMath::clamp(value, 0.0f, 1.0f); };
    m_lua["IRMath"]["lerp"] = [](float a, float b, float t) {
        return IRMath::mix(a, b, IRMath::clamp(t, 0.0f, 1.0f));
    };
    m_lua["IRMath"]["lerpByte"] = [](int a, int b, float t) {
        return static_cast<int>(
            IRMath::lerpByte(static_cast<uint8_t>(a), static_cast<uint8_t>(b), t)
        );
    };
    m_lua["IRMath"]["hsvToRgb"] = [](float h, float s, float v) {
        const IRMath::vec3 rgb = IRMath::hsvToRgb(IRMath::vec3(h, s, v));
        return std::make_tuple(rgb.r, rgb.g, rgb.b);
    };
    m_lua["IRMath"]["hsvToRgbBytes"] = [](float h, float s, float v) {
        const IRMath::u8vec3 rgbBytes = IRMath::hsvToRgbBytes(IRMath::vec3(h, s, v));
        return std::make_tuple(
            static_cast<int>(rgbBytes.r),
            static_cast<int>(rgbBytes.g),
            static_cast<int>(rgbBytes.b)
        );
    };
    m_lua["PlaneIso"] = m_lua.create_table_with(
        "XY",
        IRMath::PlaneIso::XY,
        "XZ",
        IRMath::PlaneIso::XZ,
        "YZ",
        IRMath::PlaneIso::YZ
    );
    m_lua["IRMath"]["layoutGridCentered"] = [](
                                                int index,
                                                int count,
                                                int columns,
                                                float spacingPrimary,
                                                float spacingSecondary,
                                                IRMath::PlaneIso plane,
                                                float depth
                                            ) {
        return IRMath::layoutGridCentered(
            index,
            count,
            columns,
            spacingPrimary,
            spacingSecondary,
            plane,
            depth
        );
    };
    m_lua["IRMath"]["layoutZigZagCentered"] = [](
                                                  int index,
                                                  int count,
                                                  int itemsPerZag,
                                                  float spacingPrimary,
                                                  float spacingSecondary,
                                                  IRMath::PlaneIso plane,
                                                  float depth
                                              ) {
        return IRMath::layoutZigZagCentered(
            index,
            count,
            itemsPerZag,
            spacingPrimary,
            spacingSecondary,
            plane,
            depth
        );
    };
    m_lua["IRMath"]["layoutZigZagPath"] = [](
                                                int index,
                                                int count,
                                                int itemsPerSegment,
                                                float spacingPrimary,
                                                float spacingSecondary,
                                                IRMath::PlaneIso plane,
                                                float depth
                                            ) {
        return IRMath::layoutZigZagPath(
            index,
            count,
            itemsPerSegment,
            spacingPrimary,
            spacingSecondary,
            plane,
            depth
        );
    };
    m_lua["IRMath"]["layoutSquareSpiral"] = [](
                                                 int index, float spacing, IRMath::PlaneIso plane, float depth
                                             ) {
        return IRMath::layoutSquareSpiral(index, spacing, plane, depth);
    };
    m_lua["IRMath"]["layoutHelix"] =
        [](int index, int count, float radius, float turns, float heightSpan, int axis) {
            return IRMath::layoutHelix(index, count, radius, turns, heightSpan, axis);
        };
}

LuaScript::LuaScript(const char *filename)
    : LuaScript{} {

    scriptFile(filename);
}

LuaScript::~LuaScript() {}

void LuaScript::scriptFile(const char *filename) {
    // Ensure filename is not NULL.
    IR_ASSERT(filename != nullptr, "Attempted to create LuaScript object with null file");

    try {
        // Execute the Lua script file in a protected way.
        sol::protected_function_result result = m_lua.script_file(filename);
        // sol::protected_function_result result = m_lua.safe_script(
        //     filename,
        //     &sol::script_pass_on_error
        // );

        if (!result.valid()) {
            sol::error err = result;
            IRE_LOG_ERROR("Lua script failed to load: {}. Error: {}", filename, err.what());
            return;
        }

        IRE_LOG_INFO("Lua script loaded successfully: {}", filename);
    } catch (const sol::error &e) {
        IRE_LOG_ERROR("Exception during Lua script loading ({}): {}", filename, e.what());
    } catch (const std::exception &e) {
        IRE_LOG_ERROR("Standard exception during Lua script loading ({}): {}", filename, e.what());
    }
}

sol::table LuaScript::getTable(const char *name) {
    return m_lua[name];
}

} // namespace IRScript