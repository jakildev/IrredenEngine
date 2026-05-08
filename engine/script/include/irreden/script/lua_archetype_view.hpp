#ifndef LUA_ARCHETYPE_VIEW_H
#define LUA_ARCHETYPE_VIEW_H

// Column views passed to a Lua-defined system body via the per-archetype
// `archetype` table (see `LuaScript::registerSystem`). One per matched
// component per archetype tick; lifetimes are bounded by the body
// invocation. Do NOT cache across ticks — the underlying ArchetypeNode
// can have its column vectors moved or rebuilt by structural changes
// at the next pipeline boundary.
//
// `LuaCppColumnView` covers C++ components: it dispatches per-row read
// and replace through accessors registered alongside the type's Lua
// binding (see LuaScript::registerType). `LuaTypedColumnView` covers
// Lua-defined components and goes directly through the schema-aware
// `IComponentDataLuaTyped` API for per-field access.

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <irreden/ir_profile.hpp>
#include <irreden/entity/i_component_data.hpp>
#include <irreden/script/lua_component_data.hpp>

#include <functional>
#include <string>

namespace IRScript {

// Per-row reader: given a row index, return the row's value as a
// sol::object (typically a sol::reference to T so Lua can read mutable
// fields). Captured at registerType<T> time — one entry per C++
// component type that has a Lua binding.
using LuaColumnRowReader =
    std::function<sol::object(sol::state_view, IREntity::IComponentData *, int)>;

// Per-row replacer: given a row index and a sol::object holding a value
// of the same userdata type, copy-assign the column's slot. Allows Lua
// to write back component values whose member fields aren't directly
// assignable from Lua (most existing `*_lua.hpp` bindings expose fields
// as getter lambdas, not sol::property pairs). Construct a new value
// from Lua and `setAt(i, value)` to overwrite.
using LuaColumnRowReplacer =
    std::function<void(sol::state_view, IREntity::IComponentData *, int, const sol::object &)>;

struct LuaCppColumnAccessor {
    LuaColumnRowReader reader_;
    LuaColumnRowReplacer replacer_;
};

// View of one C++ component column inside an archetype tick. Holds a
// non-owning pointer to the column's `IComponentData*` plus a pointer
// to the per-component-type accessor pair stored in LuaScript. The
// accessor pointer remains valid because LuaScript's accessor map only
// grows; references to map elements are not invalidated by rehash.
class LuaCppColumnView {
  public:
    LuaCppColumnView() = default;
    LuaCppColumnView(
        IREntity::IComponentData *data, const LuaCppColumnAccessor *accessor, int length
    )
        : m_data{data}
        , m_accessor{accessor}
        , m_length{length} {}

    sol::object at(int row, sol::this_state ts) const {
        IR_ASSERT(row >= 0 && row < m_length, "LuaCppColumnView::at row {} out of bounds [0, {})", row, m_length);
        return m_accessor->reader_(sol::state_view{ts}, m_data, row);
    }

    void setAt(int row, const sol::object &value, sol::this_state ts) const {
        IR_ASSERT(row >= 0 && row < m_length, "LuaCppColumnView::setAt row {} out of bounds [0, {})", row, m_length);
        m_accessor->replacer_(sol::state_view{ts}, m_data, row, value);
    }

    int length() const {
        return m_length;
    }

  private:
    IREntity::IComponentData *m_data = nullptr;
    const LuaCppColumnAccessor *m_accessor = nullptr;
    int m_length = 0;
};

// View of one Lua-defined component column inside an archetype tick.
// Per-field access is dispatched on the field name (linear scan;
// schemas typically have <10 fields).
class LuaTypedColumnView {
  public:
    LuaTypedColumnView() = default;
    LuaTypedColumnView(IComponentDataLuaTyped *data, int length)
        : m_data{data}
        , m_length{length} {}

    sol::object getField(int row, const std::string &fieldName, sol::this_state ts) const {
        IR_ASSERT(row >= 0 && row < m_length, "LuaTypedColumnView::getField row {} out of bounds [0, {})", row, m_length);
        const int fieldIdx = m_data->findFieldIndex(fieldName);
        if (fieldIdx < 0) {
            return sol::make_object(sol::state_view{ts}, sol::lua_nil);
        }
        return m_data->readFieldAt(row, fieldIdx, sol::state_view{ts});
    }

    void setField(int row, const std::string &fieldName, const sol::object &value) const {
        IR_ASSERT(row >= 0 && row < m_length, "LuaTypedColumnView::setField row {} out of bounds [0, {})", row, m_length);
        const int fieldIdx = m_data->findFieldIndex(fieldName);
        if (fieldIdx < 0) {
            return;
        }
        m_data->writeFieldAt(row, fieldIdx, value);
    }

    sol::table getRow(int row, sol::this_state ts) const {
        IR_ASSERT(row >= 0 && row < m_length, "LuaTypedColumnView::getRow row {} out of bounds [0, {})", row, m_length);
        return m_data->readRowAsTable(row, sol::state_view{ts});
    }

    void setRow(int row, const sol::table &values) const {
        IR_ASSERT(row >= 0 && row < m_length, "LuaTypedColumnView::setRow row {} out of bounds [0, {})", row, m_length);
        m_data->writeRowFromTable(row, values);
    }

    int length() const {
        return m_length;
    }

  private:
    IComponentDataLuaTyped *m_data = nullptr;
    int m_length = 0;
};

} // namespace IRScript

#endif /* LUA_ARCHETYPE_VIEW_H */
