#ifndef LUA_COMPONENT_DATA_H
#define LUA_COMPONENT_DATA_H

// Native-SoA storage for Lua-defined components.
//
// Each Lua-registered component owns N typed columns — one per declared
// field. Reading or writing a field is a typed vector index into the
// matching column, never a sol::table lookup. The schema (field name +
// type) is captured at IRComponent.register time and immutable; per Q4
// of the Lua-driven ECS design (docs/design/lua-driven-ecs.md), schema
// hot-reload is out of scope for v1.
//
// Lives in the script layer because sol::function and sol::table are
// first-class storable types. The base IComponentData interface (entity
// layer) is C++-only; entity sees IComponentDataLuaTyped only via the
// virtual interface, so the layering stays one-way (script → entity).

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <irreden/entity/i_component_data.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/script/ir_script_utils.hpp>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace IRScript {

// Native-storage tag for one Lua-registered field. SoA columns are
// dispatched on this enum: each field has exactly one column type. Q1 of
// the Lua-driven ECS design intentionally has no implicit fallback — a
// field whose default value cannot be classified raises an error at
// IRComponent.register time, before any storage is allocated.
enum class LuaFieldType : std::uint8_t {
    INT32,
    FLOAT,
    BOOL,
    STRING,
    FUNCTION, // sol::function — per-entity callbacks (onDeath, behavior)
    TABLE,    // explicit opt-in only; pays per-access sol::table cost
    VEC3,     // IRMath::vec3 — packed 3-float field (G1a, docs/design/lua-driven-ecs.md)
    IVEC3,    // IRMath::ivec3 — packed 3-int field
    VEC4,     // IRMath::vec4 — packed 4-float field; also the quat/quaternion tag alias
};

inline const char *toString(LuaFieldType t) {
    switch (t) {
    case LuaFieldType::INT32:
        return "int32";
    case LuaFieldType::FLOAT:
        return "float";
    case LuaFieldType::BOOL:
        return "bool";
    case LuaFieldType::STRING:
        return "string";
    case LuaFieldType::FUNCTION:
        return "function";
    case LuaFieldType::TABLE:
        return "table";
    case LuaFieldType::VEC3:
        return "vec3";
    case LuaFieldType::IVEC3:
        return "ivec3";
    case LuaFieldType::VEC4:
        return "vec4";
    }
    return "?";
}

struct LuaFieldSchema {
    std::string name_;
    LuaFieldType type_;
    sol::object default_; // bound to the schema's owning sol::state
};

// One IComponentDataLuaTyped impl owns one column-vector per declared
// field. The variant is matched 1:1 to LuaFieldType ordering so
// dispatch is a switch on the enum.
using LuaFieldColumn = std::variant<
    std::vector<std::int32_t>,
    std::vector<float>,
    std::vector<std::uint8_t>, // bool stored as uint8 to keep std::vector indexable as &[i]
    std::vector<std::string>,
    std::vector<sol::function>,
    std::vector<sol::table>,
    std::vector<IRMath::vec3>,
    std::vector<IRMath::ivec3>,
    std::vector<IRMath::vec4>>;

namespace detail {

inline LuaFieldColumn makeEmptyColumn(LuaFieldType t) {
    switch (t) {
    case LuaFieldType::INT32:
        return std::vector<std::int32_t>{};
    case LuaFieldType::FLOAT:
        return std::vector<float>{};
    case LuaFieldType::BOOL:
        return std::vector<std::uint8_t>{};
    case LuaFieldType::STRING:
        return std::vector<std::string>{};
    case LuaFieldType::FUNCTION:
        return std::vector<sol::function>{};
    case LuaFieldType::TABLE:
        return std::vector<sol::table>{};
    case LuaFieldType::VEC3:
        return std::vector<IRMath::vec3>{};
    case LuaFieldType::IVEC3:
        return std::vector<IRMath::ivec3>{};
    case LuaFieldType::VEC4:
        return std::vector<IRMath::vec4>{};
    }
    return std::vector<std::int32_t>{};
}

inline int columnSize(const LuaFieldColumn &c) {
    return std::visit([](const auto &v) { return static_cast<int>(v.size()); }, c);
}

inline void columnAppendDefault(LuaFieldColumn &c, const sol::object &defaultValue) {
    std::visit(
        [&](auto &v) {
            using V = std::decay_t<decltype(v)>;
            using Elem = typename V::value_type;
            if constexpr (std::is_same_v<Elem, std::int32_t>) {
                v.push_back(defaultValue.is<int>() ? defaultValue.as<int>() : 0);
            } else if constexpr (std::is_same_v<Elem, float>) {
                v.push_back(defaultValue.is<float>() ? defaultValue.as<float>() : 0.0f);
            } else if constexpr (std::is_same_v<Elem, std::uint8_t>) {
                v.push_back(defaultValue.is<bool>() ? (defaultValue.as<bool>() ? 1u : 0u) : 0u);
            } else if constexpr (std::is_same_v<Elem, std::string>) {
                v.push_back(
                    defaultValue.is<std::string>() ? defaultValue.as<std::string>() : std::string{}
                );
            } else if constexpr (std::is_same_v<Elem, sol::function>) {
                v.push_back(
                    defaultValue.is<sol::function>() ? defaultValue.as<sol::function>()
                                                     : sol::function{}
                );
            } else if constexpr (std::is_same_v<Elem, sol::table>) {
                v.push_back(
                    defaultValue.is<sol::table>() ? defaultValue.as<sol::table>() : sol::table{}
                );
            } else if constexpr (std::is_same_v<Elem, IRMath::vec3>) {
                v.push_back(vec3FromLua(defaultValue));
            } else if constexpr (std::is_same_v<Elem, IRMath::ivec3>) {
                v.push_back(ivec3FromLua(defaultValue));
            } else if constexpr (std::is_same_v<Elem, IRMath::vec4>) {
                // quatFromLua identity-defaults nil/partial to (0,0,0,1) — the
                // correct default for the primary rotation consumer; a plain
                // vec4 author passes an explicit 4-element default.
                v.push_back(quatFromLua(defaultValue));
            }
        },
        c
    );
}

inline void columnMoveAndPack(LuaFieldColumn &src, LuaFieldColumn &dest, int srcIdx) {
    std::visit(
        [srcIdx](auto &s, auto &d) {
            using S = std::decay_t<decltype(s)>;
            using D = std::decay_t<decltype(d)>;
            if constexpr (std::is_same_v<S, D>) {
                d.push_back(std::move(s[srcIdx]));
                s[srcIdx] = std::move(s.back());
                s.pop_back();
            }
        },
        src,
        dest
    );
}

inline void columnPushCopy(const LuaFieldColumn &src, LuaFieldColumn &dest, int srcIdx) {
    std::visit(
        [srcIdx](const auto &s, auto &d) {
            using S = std::decay_t<decltype(s)>;
            using D = std::decay_t<decltype(d)>;
            if constexpr (std::is_same_v<S, D>) {
                d.push_back(s[srcIdx]);
            }
        },
        src,
        dest
    );
}

inline void columnSwapRemove(LuaFieldColumn &c, int idx) {
    std::visit(
        [idx](auto &v) {
            v[idx] = std::move(v.back());
            v.pop_back();
        },
        c
    );
}

} // namespace detail

class IComponentDataLuaTyped : public IREntity::IComponentData {
  public:
    explicit IComponentDataLuaTyped(std::vector<LuaFieldSchema> schema)
        : m_schema{std::move(schema)} {
        m_columns.reserve(m_schema.size());
        for (const auto &f : m_schema) {
            m_columns.emplace_back(detail::makeEmptyColumn(f.type_));
        }
    }

    int size() const override {
        if (m_columns.empty())
            return 0;
        return detail::columnSize(m_columns.front());
    }

    IREntity::smart_ComponentData cloneEmpty() const override {
        return std::make_unique<IComponentDataLuaTyped>(m_schema);
    }

    void moveDataAndPack(IREntity::IComponentData *dest, const int indexSource) override {
        auto *castedDest = static_cast<IComponentDataLuaTyped *>(dest);
        IR_ASSERT(
            castedDest->m_schema.size() == m_schema.size(),
            "Lua-typed component archetype move: schema-arity mismatch"
        );
        for (std::size_t i = 0; i < m_columns.size(); ++i) {
            detail::columnMoveAndPack(m_columns[i], castedDest->m_columns[i], indexSource);
        }
    }

    void pushCopyData(IREntity::IComponentData *dest, const int indexSource) override {
        auto *castedDest = static_cast<IComponentDataLuaTyped *>(dest);
        for (std::size_t i = 0; i < m_columns.size(); ++i) {
            detail::columnPushCopy(m_columns[i], castedDest->m_columns[i], indexSource);
        }
    }

    void removeDataAndPack(const int index) override {
        for (auto &col : m_columns) {
            detail::columnSwapRemove(col, index);
        }
    }

    void destroy(const int /*index*/) override {
        // Lua-typed components have no per-entity onDestroy hook — sol2
        // handles ref-count cleanup of sol::function / sol::table when
        // the column entry is overwritten or popped.
    }

    bool appendDefaultRow() override {
        for (std::size_t i = 0; i < m_columns.size(); ++i) {
            detail::columnAppendDefault(m_columns[i], m_schema[i].default_);
        }
        return true;
    }

    const std::vector<LuaFieldSchema> &schema() const {
        return m_schema;
    }

    // Returns the column index for `fieldName`, or -1 if absent. Linear
    // scan; field counts are small (typically <10) so this beats hashing.
    int findFieldIndex(std::string_view fieldName) const {
        for (std::size_t i = 0; i < m_schema.size(); ++i) {
            if (m_schema[i].name_ == fieldName)
                return static_cast<int>(i);
        }
        return -1;
    }

    LuaFieldColumn &columnAt(int fieldIdx) {
        return m_columns[fieldIdx];
    }

    const LuaFieldColumn &columnAt(int fieldIdx) const {
        return m_columns[fieldIdx];
    }

    // Apply a partial overwrite from a Lua values table to row `row`. Only
    // fields present in `values` are touched; missing fields keep their
    // existing column value (which, for a freshly appendDefaultRow'd row,
    // is the schema default). Unknown keys are silently ignored — a
    // stricter validator can land later if it proves useful.
    void writeRowFromTable(int row, const sol::table &values) {
        for (auto &kv : values) {
            sol::optional<std::string> keyName = kv.first.as<sol::optional<std::string>>();
            if (!keyName)
                continue;
            const int fieldIdx = findFieldIndex(*keyName);
            if (fieldIdx < 0)
                continue;
            writeFieldAt(row, fieldIdx, kv.second);
        }
    }

    void writeFieldAt(int row, int fieldIdx, const sol::object &value) {
        std::visit(
            [&](auto &v) {
                using V = std::decay_t<decltype(v)>;
                using Elem = typename V::value_type;
                if constexpr (std::is_same_v<Elem, std::int32_t>) {
                    if (value.is<int>())
                        v[row] = value.as<int>();
                } else if constexpr (std::is_same_v<Elem, float>) {
                    if (value.is<float>())
                        v[row] = value.as<float>();
                } else if constexpr (std::is_same_v<Elem, std::uint8_t>) {
                    if (value.is<bool>())
                        v[row] = value.as<bool>() ? 1u : 0u;
                } else if constexpr (std::is_same_v<Elem, std::string>) {
                    if (value.is<std::string>())
                        v[row] = value.as<std::string>();
                } else if constexpr (std::is_same_v<Elem, sol::function>) {
                    if (value.is<sol::function>())
                        v[row] = value.as<sol::function>();
                } else if constexpr (std::is_same_v<Elem, sol::table>) {
                    if (value.is<sol::table>())
                        v[row] = value.as<sol::table>();
                } else if constexpr (std::is_same_v<Elem, IRMath::vec3>) {
                    if (value.is<sol::table>() || value.is<IRMath::vec3>())
                        v[row] = vec3FromLua(value);
                } else if constexpr (std::is_same_v<Elem, IRMath::ivec3>) {
                    if (value.is<sol::table>() || value.is<IRMath::ivec3>())
                        v[row] = ivec3FromLua(value);
                } else if constexpr (std::is_same_v<Elem, IRMath::vec4>) {
                    if (value.is<sol::table>() || value.is<IRMath::vec4>())
                        v[row] = quatFromLua(value);
                }
            },
            m_columns[fieldIdx]
        );
    }

    sol::object readFieldAt(int row, int fieldIdx, sol::state_view lua) const {
        return std::visit(
            [&](const auto &v) -> sol::object {
                using V = std::decay_t<decltype(v)>;
                using Elem = typename V::value_type;
                if constexpr (std::is_same_v<Elem, std::uint8_t>) {
                    return sol::make_object(lua, v[row] != 0);
                } else if constexpr (
                    std::is_same_v<Elem, IRMath::vec3> || std::is_same_v<Elem, IRMath::ivec3>
                ) {
                    // vec3 / ivec3 surface to Lua as an { x, y, z } table —
                    // round-trips through writeFieldAt (vec3FromLua accepts the
                    // same shape) without requiring the creation to register an
                    // IRMath::vec3 usertype. Index-style hot paths that need
                    // zero-alloc per-component reads use three scalar fields.
                    const Elem &val = v[row];
                    return lua.create_table_with("x", val.x, "y", val.y, "z", val.z);
                } else if constexpr (std::is_same_v<Elem, IRMath::vec4>) {
                    // vec4 / quat surfaces as an { x, y, z, w } table — same
                    // round-trip contract as vec3 (writeFieldAt's quatFromLua
                    // accepts the same shape), with .w the quaternion scalar.
                    const IRMath::vec4 &val = v[row];
                    return lua.create_table_with("x", val.x, "y", val.y, "z", val.z, "w", val.w);
                } else {
                    return sol::make_object(lua, v[row]);
                }
            },
            m_columns[fieldIdx]
        );
    }

    sol::table readRowAsTable(int row, sol::state_view lua) const {
        sol::table t = lua.create_table();
        for (std::size_t i = 0; i < m_schema.size(); ++i) {
            t[m_schema[i].name_] = readFieldAt(row, static_cast<int>(i), lua);
        }
        return t;
    }

  private:
    std::vector<LuaFieldSchema> m_schema;
    std::vector<LuaFieldColumn> m_columns;
};

} // namespace IRScript

#endif /* LUA_COMPONENT_DATA_H */
