#include <irreden/ir_profile.hpp>

#include <irreden/script/lua_script.hpp>

#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/modifier_field_registry.hpp>
#include <irreden/script/lua_audio_bindings.hpp>
#include <irreden/script/lua_collision_bindings.hpp>
#include <irreden/script/lua_command_bindings.hpp>
#include <irreden/script/lua_input_bindings.hpp>
#include <irreden/script/lua_enum_def.hpp>
#include <irreden/script/lua_modifier_bindings.hpp>
#include <irreden/script/lua_persistence_bindings.hpp>
#include <irreden/script/lua_pipeline_bindings.hpp>
#include <irreden/script/lua_render_bindings.hpp>
#include <irreden/script/lua_sim_bindings.hpp>
#include <irreden/script/lua_spatial_bindings.hpp>
#include <irreden/script/lua_widget_bindings.hpp>
#include <irreden/script/lua_world_snapshot_bindings.hpp>
#include <irreden/script/prefab_api.hpp>
#include <irreden/voxel/components/component_bind_points.hpp>
#include <irreden/voxel/components/component_joint_hierarchy.hpp>
#include <irreden/voxel/rig_bridge.hpp>

#include <deque>
#include <string>
#include <vector>

namespace IRScript {

namespace {

// Stable storage for field-binding names registered from Lua. The
// modifier framework's FieldRegistry stores `const char*` and assumes
// static-storage lifetime for those pointers (see
// engine/prefabs/irreden/common/modifier_field_registry.hpp). Lua scripts
// produce names at runtime, so we own them here in a deque (pointer-
// stable across pushes) for the lifetime of the process. Registered
// names are intentionally never freed — modifier ids are issued once
// and consumers may keep them across world restarts.
std::deque<std::string> &luaFieldBindingNames() {
    static std::deque<std::string> names;
    return names;
}

// Modifier-targetable field types — only these auto-register a
// FieldBindingId because the modifier resolver writes float scalars
// (see docs/design/modifiers.md). String / function / table fields are
// stored natively but cannot be modifier targets.
bool isModifierTargetable(LuaFieldType t) {
    return t == LuaFieldType::INT32 || t == LuaFieldType::FLOAT || t == LuaFieldType::BOOL;
}

LuaFieldType inferTypeFromDefault(const sol::object &value) {
    if (value.is<bool>())
        return LuaFieldType::BOOL;
    if (value.is<int>())
        return LuaFieldType::INT32;
    if (value.is<float>())
        return LuaFieldType::FLOAT;
    if (value.is<std::string>())
        return LuaFieldType::STRING;
    // Packed vector userdata: only matches if the creation registered an
    // IRMath::vec3 / IRMath::ivec3 sol usertype (e.g. `vec3.new(...)`). When
    // unregistered, `is<>` is false and the explicit `{ type = "vec3" }` tag
    // form (parseExplicitTypeTag) remains the portable path.
    if (value.is<IRMath::ivec3>())
        return LuaFieldType::IVEC3;
    if (value.is<IRMath::vec3>())
        return LuaFieldType::VEC3;
    // Only fires when the creation registered an IRMath::vec4 sol usertype; a
    // bare { x, y, z, w } table still needs the explicit tag, as vec3 does.
    if (value.is<IRMath::vec4>())
        return LuaFieldType::VEC4;
    if (value.is<sol::function>())
        return LuaFieldType::FUNCTION;
    // Numeric values with a fractional part: under LuaJIT (Lua 5.1
    // base, no integer subtype), `is<int>` already classified
    // whole-number literals like `0.0` as INT32 above — only values
    // with a non-zero fractional part reach this fallback. See
    // engine/script/CLAUDE.md "Lua runtime: LuaJIT 2.1" for the
    // user-visible disambiguation contract.
    if (value.is<double>())
        return LuaFieldType::FLOAT;
    // Anything else — including nested tables — is a registration-time
    // error per Q1 of the design (no silent sol::table fallback).
    return LuaFieldType::TABLE; // sentinel: caller checks this and rejects
}

LuaFieldType parseExplicitTypeTag(const std::string &tag, bool &ok) {
    ok = true;
    if (tag == "int" || tag == "int32" || tag == "integer")
        return LuaFieldType::INT32;
    if (tag == "float" || tag == "number")
        return LuaFieldType::FLOAT;
    if (tag == "bool" || tag == "boolean")
        return LuaFieldType::BOOL;
    if (tag == "string")
        return LuaFieldType::STRING;
    if (tag == "function")
        return LuaFieldType::FUNCTION;
    if (tag == "table")
        return LuaFieldType::TABLE;
    if (tag == "vec3")
        return LuaFieldType::VEC3;
    if (tag == "ivec3")
        return LuaFieldType::IVEC3;
    if (tag == "vec4" || tag == "quat" || tag == "quaternion")
        return LuaFieldType::VEC4;
    ok = false;
    return LuaFieldType::TABLE;
}

// Build a single field schema from one (key, value) entry of the
// defaults table. Supports both the short form (`current = 100`) and
// the explicit form (`current = { type = "float", default = 100 }`).
// Throws sol::error (caught by the protected_function path) on any
// type-inference failure so the user sees a Lua-side error pointing at
// the offending field.
LuaFieldSchema buildFieldSchema(
    const std::string &componentName, const std::string &fieldName, const sol::object &raw
) {
    LuaFieldSchema s;
    s.name_ = fieldName;

    if (raw.is<sol::table>()) {
        sol::table t = raw.as<sol::table>();
        sol::optional<std::string> tag = t.get<sol::optional<std::string>>("type");
        sol::object dflt = t.get<sol::object>("default");
        if (tag) {
            bool ok = false;
            const LuaFieldType inferred = parseExplicitTypeTag(*tag, ok);
            if (!ok) {
                throw sol::error{
                    "IRComponent.register: " + componentName + "." + fieldName +
                    " has unknown type tag '" + *tag +
                    "' (expected one of int|float|bool|string|function|table|vec3|ivec3|vec4)"
                };
            }
            s.type_ = inferred;
            s.default_ = dflt;
            return s;
        }
        // No explicit `type` tag in the inner table → user wanted the
        // short form with a literal table value, which is a Q1
        // registration error (no implicit table fallback).
        throw sol::error{
            "IRComponent.register: " + componentName + "." + fieldName +
            " has a table default with no `type` tag — use `{ type = \"table\", default = {...} }` "
            "to opt in"
        };
    }

    const LuaFieldType inferred = inferTypeFromDefault(raw);
    if (inferred == LuaFieldType::TABLE) {
        // Sentinel from inferTypeFromDefault — the value didn't match
        // any native column type. Hard fail with the field name.
        throw sol::error{
            "IRComponent.register: " + componentName + "." + fieldName +
            " has unsupported default value (cannot infer native type — "
            "use { type = \"...\", default = ... } to disambiguate)"
        };
    }
    s.type_ = inferred;
    s.default_ = raw;
    return s;
}

// Add a dynamic (Lua-registered / codegen'd) component to `entity` by
// ComponentId and, when `overrides` is present, write its fields from the
// table. Shared by the eager `IREntity.addLuaComponent` binding and the
// deferred `IREntity.deferredCreate` attach path.
void attachDynamicComponent(
    IREntity::EntityManager &em,
    IREntity::EntityId entity,
    IREntity::ComponentId componentId,
    const sol::optional<sol::table> &overrides
) {
    em.addComponentDynamic(entity, componentId);
    if (!overrides) {
        return;
    }
    auto [data, row] = em.getComponentDataAndRow(entity, componentId);
    IR_ASSERT(data != nullptr, "attachDynamicComponent: post-add lookup failed");
    auto *typed = static_cast<IComponentDataLuaTyped *>(data);
    typed->writeRowFromTable(row, *overrides);
}

} // namespace

// lua_dofile runs a lua script. Global functions and variables
// can be accessed via the lua stack.
LuaScript::LuaScript()
    : m_lua{} {
    m_lua.open_libraries(
        sol::lib::base,
        sol::lib::package,
        sol::lib::string,
        sol::lib::table,
        sol::lib::math,
        // sol's `bit32` slot wires LuaJIT's `luaopen_bit` (the same C
        // function backing LuaJIT's `bit` module) via
        // `luaL_requiref(L, "bit32", luaopen_bit, 1)`. The global lands
        // under `bit32`; alias it to `bit` below so creations can spell
        // the canonical LuaJIT idiom `bit.bor(a, b, c)` per the
        // docs/design/lua-input-commands.md Q6 modifier-mask contract
        // and the engine/script/CLAUDE.md statement that `bit` is
        // universally available.
        sol::lib::bit32
    );
    m_lua.safe_script("if bit == nil and bit32 ~= nil then bit = bit32 end");

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
    m_lua["CoordinateAxis"] = m_lua.create_table_with(
        "XAxis",
        static_cast<int>(IRMath::CoordinateAxis::XAxis),
        "YAxis",
        static_cast<int>(IRMath::CoordinateAxis::YAxis),
        "ZAxis",
        static_cast<int>(IRMath::CoordinateAxis::ZAxis)
    );
    m_lua["IRMath"]["layoutGridCentered"] = [](int index,
                                               int count,
                                               int columns,
                                               float spacingPrimary,
                                               float spacingSecondary,
                                               IRMath::PlaneIso plane,
                                               float depth) {
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
    m_lua["IRMath"]["layoutZigZagCentered"] = [](int index,
                                                 int count,
                                                 int itemsPerZag,
                                                 float spacingPrimary,
                                                 float spacingSecondary,
                                                 IRMath::PlaneIso plane,
                                                 float depth) {
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
    m_lua["IRMath"]["layoutZigZagPath"] = [](int index,
                                             int count,
                                             int itemsPerSegment,
                                             float spacingPrimary,
                                             float spacingSecondary,
                                             IRMath::PlaneIso plane,
                                             float depth) {
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
    m_lua["IRMath"]["layoutSquareSpiral"] =
        [](int index, float spacing, IRMath::PlaneIso plane, float depth) {
            return IRMath::layoutSquareSpiral(index, spacing, plane, depth);
        };
    m_lua["IRMath"]["layoutCircle"] = [](int index,
                                         int count,
                                         float radius,
                                         IRMath::PlaneIso plane,
                                         float depth,
                                         sol::optional<float> startAngleRad) {
        const float angle = startAngleRad.value_or(-1.57079633f); // -pi/2 = top
        return IRMath::layoutCircle(index, count, radius, angle, plane, depth);
    };
    m_lua["IRMath"]["layoutHelix"] =
        [](int index, int count, float radius, float turns, float heightSpan, int axis) {
            return IRMath::layoutHelix(
                index,
                count,
                radius,
                turns,
                heightSpan,
                static_cast<IRMath::CoordinateAxis>(axis)
            );
        };
    m_lua["IRMath"]["layoutPathTangentArcs"] = [](int index,
                                                  int count,
                                                  float radius,
                                                  int blocksPerArc,
                                                  float zStep,
                                                  int axis,
                                                  sol::optional<float> startAngleRad,
                                                  sol::optional<bool> invert) {
        const float angle = startAngleRad.value_or(0.785398163f);
        const bool inv = invert.value_or(false);
        return IRMath::layoutPathTangentArcs(
            index,
            count,
            radius,
            blocksPerArc,
            zStep,
            static_cast<IRMath::CoordinateAxis>(axis),
            angle,
            inv
        );
    };
}

LuaScript::LuaScript(const char *filename)
    : LuaScript{} {

    scriptFile(filename);
}

LuaScript::~LuaScript() {
    // Release sol::protected_function references held by m_luaSystemTicks
    // BEFORE m_lua's destructor closes the Lua state. Member destruction
    // runs in reverse declaration order — m_lua is declared last so it
    // destructs first, which is correct for the Lua-ref-free maps
    // (m_prefabSystemIds etc.) but wrong for m_luaSystemTicks's
    // shared_ptr<sol::protected_function> values: those would call
    // luaL_unref against a dead lua_State and crash. Explicitly clear
    // here so the ref releases happen while Lua is still alive. Body
    // lambdas owned by SystemManager have already been destroyed by the
    // time LuaScript's dtor runs (see World's m_lua → m_entityManager →
    // m_systemManager declaration order, reverse-destructed), so this
    // map is the last surviving owner of those refs.
    m_luaSystemTicks.clear();
}

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

void LuaScript::bindLuaDrivenEcs() {
    // Idempotent: re-binding would overwrite identical Lua tables but
    // the C++ side would re-register IComponent types in the entity
    // manager — which would fail by name collision. Guard once.
    if (m_lua["IRComponent"].valid()) {
        return;
    }

    m_lua["IRComponent"] = m_lua.create_table();

    auto registerComponent =
        [this](const std::string &componentName, sol::table defaults) -> sol::object {
        auto &em = IREntity::getEntityManager();
        // Coexistence-mode idempotency. The same .lua file is consumed by
        // the build-time codegen tool AND loaded again at runtime;
        // components declared via `IRComponent.register("Foo", {...})` get
        // codegen'd as C++ structs (`IRComponents::C_Foo`) and pre-
        // registered by `registerCodegenComponents()`. The runtime call
        // would otherwise create a parallel IComponentDataLuaTyped under
        // the same Lua name, splitting archetype storage. Detect the
        // C++-codegen'd case via the LuaScript-side `componentByLuaName`
        // map (populated by `registerType<T>` → `recordComponentLuaName<T>`)
        // and return the existing handle without re-registering. Lua code
        // paths that depend on `handle.fields.<name>.bindingId` (modifier
        // framework) do not work for codegen'd-as-C++ components in
        // coexistence mode — that's an EVAL-only feature documented in
        // CLAUDE.md.
        const IREntity::ComponentId existingCpp = componentIdByLuaName(componentName);
        if (existingCpp != IREntity::kNullComponent) {
            sol::object existingHandle = m_lua["IRComponent"][componentName];
            if (existingHandle.is<sol::table>()) {
                return existingHandle;
            }
        }
        if (em.isComponentRegistered(componentName)) {
            throw sol::error{"IRComponent.register: '" + componentName + "' is already registered"};
        }

        std::vector<LuaFieldSchema> schema;
        schema.reserve(8);
        for (auto &kv : defaults) {
            sol::optional<std::string> keyName = kv.first.as<sol::optional<std::string>>();
            if (!keyName) {
                throw sol::error{
                    "IRComponent.register: '" + componentName + "' has a non-string field key"
                };
            }
            schema.push_back(buildFieldSchema(componentName, *keyName, kv.second));
        }

        std::vector<IRComponents::FieldBindingId> fieldIds;
        fieldIds.reserve(schema.size());
        for (const auto &f : schema) {
            if (!isModifierTargetable(f.type_)) {
                fieldIds.push_back(IRComponents::kInvalidFieldId);
                continue;
            }
            auto &names = luaFieldBindingNames();
            names.emplace_back(componentName + "." + f.name_);
            const auto id = IRPrefab::Modifier::detail::globalFieldRegistry().registerField(
                names.back().c_str()
            );
            fieldIds.push_back(id);
        }

        auto impl = std::make_unique<IComponentDataLuaTyped>(schema);
        const IREntity::ComponentId componentId =
            em.registerComponentDynamic(componentName, std::move(impl));
        IR_ASSERT(
            componentId != IREntity::kNullComponent,
            "registerComponentDynamic returned kNullComponent for {} (duplicate slipped past "
            "isComponentRegistered check)",
            componentName.c_str()
        );

        sol::table handle = m_lua.create_table();
        handle["typeName"] = componentName;
        handle["componentId"] = static_cast<lua_Integer>(componentId);
        sol::table fieldsTable = m_lua.create_table();
        for (std::size_t i = 0; i < schema.size(); ++i) {
            sol::table fieldEntry = m_lua.create_table();
            fieldEntry["name"] = schema[i].name_;
            fieldEntry["type"] = std::string{toString(schema[i].type_)};
            fieldEntry["bindingId"] = static_cast<lua_Integer>(fieldIds[i]);
            fieldEntry["index"] = static_cast<lua_Integer>(i);
            fieldsTable[schema[i].name_] = fieldEntry;
        }
        handle["fields"] = fieldsTable;
        return sol::make_object(m_lua, handle);
    };

    m_lua["IRComponent"]["register"] = registerComponent;

    // Enum-typed schema values get a Lua table mirror so prefab files and
    // creation scripts spell them as `IRComponent.X.Y` rather than the
    // string "Y" — keeps Lua-side authoring in lockstep with the C++ enum
    // and avoids string-name lookups in the C++ binding code (see
    // .claude/rules/cpp-lua-enums.md).
    sol::table rotationModeTable = m_lua.create_table();
#define IR_BIND_ROTMODE(name)                                                                      \
    rotationModeTable[#name] = static_cast<lua_Integer>(IRComponents::RotationMode::name)
    IR_BIND_ROTMODE(GRID);
    IR_BIND_ROTMODE(DETACHED);
    IR_BIND_ROTMODE(DETACHED_REVOXELIZE);
#undef IR_BIND_ROTMODE
    m_lua["IRComponent"]["RotationMode"] = rotationModeTable;

    // Shared detail::registerLuaEnum guarantees identical ordinals at build time and runtime.
    m_lua["IREnum"] = m_lua.create_table();
    m_lua["IREnum"]["register"] =
        [this](const std::string &enumName, sol::table members) -> sol::object {
        return sol::make_object(
            m_lua,
            detail::registerLuaEnum(m_lua, m_luaEnumNames, enumName, members)
        );
    };

    m_lua.new_usertype<IRScript::LuaEntity>(
        "LuaEntity",
        sol::constructors<IRScript::LuaEntity(IREntity::EntityId)>(),
        "entity",
        &IRScript::LuaEntity::entity
    );

    if (!m_lua["IREntity"].valid()) {
        m_lua["IREntity"] = m_lua.create_table();
    }

    // #1814: world-level scene-transition teardown. Destroys every gameplay
    // entity, preserving singletons + C_Persistent-tagged entities (the
    // renderer's camera/canvas survive). The scene machine pairs this with
    // IRSystem.clearPipeline / registerPipeline at a frame boundary.
    if (!m_lua["IRWorld"].valid()) {
        m_lua["IRWorld"] = m_lua.create_table();
    }
    m_lua["IRWorld"]["resetGameplay"] = []() { IREntity::resetGameplay(); };

    m_lua["IREntity"]["addLuaComponent"] = [this](
                                               IRScript::LuaEntity entity,
                                               sol::table componentDef,
                                               sol::optional<sol::table> overrides
                                           ) {
        const IREntity::ComponentId componentId = componentDef.get<lua_Integer>("componentId");
        attachDynamicComponent(IREntity::getEntityManager(), entity.entity, componentId, overrides);
    };

    m_lua["IREntity"]["getLuaComponent"] =
        [this](IRScript::LuaEntity entity, sol::table componentDef) -> sol::object {
        const IREntity::ComponentId componentId = componentDef.get<lua_Integer>("componentId");
        auto &em = IREntity::getEntityManager();
        auto [data, row] = em.getComponentDataAndRow(entity.entity, componentId);
        if (!data)
            return sol::make_object(m_lua, sol::lua_nil);
        auto *typed = static_cast<IComponentDataLuaTyped *>(data);
        return sol::make_object(m_lua, typed->readRowAsTable(row, m_lua));
    };

    m_lua["IREntity"]["removeLuaComponent"] = [](IRScript::LuaEntity entity,
                                                 sol::table componentDef) {
        const IREntity::ComponentId componentId = componentDef.get<lua_Integer>("componentId");
        IREntity::getEntityManager().removeComponentDynamic(entity.entity, componentId);
    };

    m_lua["IREntity"]["hasLuaComponent"] = [](IRScript::LuaEntity entity, sol::table componentDef) {
        const IREntity::ComponentId componentId = componentDef.get<lua_Integer>("componentId");
        return IREntity::getEntityManager().hasComponent(entity.entity, componentId);
    };

    // #2286: deferred entity create/destroy for EVAL Lua systems. A structural
    // change (create, destroy) issued from inside a per-entity tick would
    // invalidate the archetype iteration in progress, so these route through
    // the same deferred machinery C++ systems use: the create's archetype
    // insert drains at the next `flushStructuralChanges` (a group boundary),
    // the destroy at `destroyMarkedEntities` (pipeline end) — never mid-tick.
    // See `docs/design/lua-driven-ecs.md` §G4.
    //
    // `deferredCreate` returns the reserved EntityId immediately so a tick can
    // hold it (e.g. to `deferredDestroy` it later, or stash it in a component)
    // before the entity actually materializes at flush. `componentList` is an
    // optional array of `{ componentDef, overridesTableOrNil }` entries:
    // `componentDef` is the table from `IRComponent.register` / a codegen'd
    // component binding (it carries `componentId`), and the optional overrides
    // are applied exactly like `addLuaComponent`. Both Lua-registered and
    // codegen'd components share the ComponentId space, so either attaches.
    m_lua["IREntity"]["deferredCreate"] =
        [this](sol::optional<sol::table> componentList) -> IREntity::EntityId {
        auto &em = IREntity::getEntityManager();
        // Marshal the sol values on the calling (tick) thread; the deferred
        // lambda then attaches without re-entering Lua at flush time. Each
        // pending entry is (componentId, optional field-overrides table).
        using PendingComponent = std::pair<IREntity::ComponentId, sol::optional<sol::table>>;
        std::vector<PendingComponent> pending;
        if (componentList) {
            const std::size_t count = componentList->size();
            pending.reserve(count);
            for (std::size_t i = 1; i <= count; ++i) {
                sol::table entry = componentList->get<sol::table>(i);
                sol::table componentDef = entry.get<sol::table>(1);
                pending.emplace_back(
                    componentDef.get<lua_Integer>("componentId"),
                    entry.get<sol::optional<sol::table>>(2)
                );
            }
        }
        const IREntity::EntityId entity = em.createEntityDeferred();
        em.stageStructuralChange([entity, pending = std::move(pending)]() {
            auto &mgr = IREntity::getEntityManager();
            for (const auto &[componentId, overrides] : pending) {
                attachDynamicComponent(mgr, entity, componentId, overrides);
            }
        });
        return entity;
    };

    m_lua["IREntity"]["deferredDestroy"] = [](IREntity::EntityId entity) {
        // markEntityForDeletion mutates the flag bit on its arg ref; the local
        // copy absorbs it. Drained by destroyMarkedEntities at pipeline end.
        IREntity::getEntityManager().markEntityForDeletion(entity);
    };

    m_lua["IREntity"]["bindPoint"] = [](IRScript::LuaEntity self,
                                        const std::string &name,
                                        sol::this_state L) -> std::tuple<sol::object, sol::object> {
        sol::state_view sv{L};
        auto returnNil = [&]() {
            return std::make_tuple(
                sol::make_object(sv, sol::lua_nil),
                sol::make_object(sv, sol::lua_nil)
            );
        };
        auto bindPointsOpt =
            IREntity::getComponentOptional<IRComponents::C_BindPoints>(self.entity);
        if (!bindPointsOpt.has_value()) {
            return returnNil();
        }
        const auto *bindPoints = bindPointsOpt.value();
        auto it = bindPoints->points_.find(name);
        if (it == bindPoints->points_.end()) {
            return returnNil();
        }
        auto hierarchyOpt =
            IREntity::getComponentOptional<IRComponents::C_JointHierarchy>(self.entity);
        IRPrefab::Rig::BindPointWorldTransform world;
        if (hierarchyOpt.has_value()) {
            world = IRPrefab::Rig::worldTransformForBindPoint(it->second, *hierarchyOpt.value());
        } else {
            world.offset_ = it->second.offset_;
            world.rotation_ = it->second.rotation_;
        }
        return std::make_tuple(
            sol::make_object(sv, world.offset_),
            sol::make_object(sv, world.rotation_)
        );
    };

    // ECS singleton-component lookup. One entity per component type,
    // lazily created on first call and cached by `ComponentId`. Returns
    // a `LuaEntity` so callers can chain into the standard
    // `getLuaComponent` / `setLuaField` accessors. Works for both
    // codegen'd-as-C++ components and runtime-registered Lua-defined
    // components — both paths share the same `ComponentId` space and the
    // same singleton cache on `EntityManager`.
    m_lua["IREntity"]["singleton"] = [](sol::table componentDef) -> IRScript::LuaEntity {
        const IREntity::ComponentId componentId = componentDef.get<lua_Integer>("componentId");
        return IRScript::LuaEntity{
            IREntity::getEntityManager().getOrCreateSingletonByComponentId(componentId)
        };
    };

    m_lua["IREntity"]["getLuaField"] =
        [this](IRScript::LuaEntity entity, sol::table componentDef, int fieldIndex) -> sol::object {
        const IREntity::ComponentId componentId = componentDef.get<lua_Integer>("componentId");
        auto &em = IREntity::getEntityManager();
        auto [data, row] = em.getComponentDataAndRow(entity.entity, componentId);
        if (!data)
            return sol::make_object(m_lua, sol::lua_nil);
        auto *typed = static_cast<IComponentDataLuaTyped *>(data);
        const int schemaSize = static_cast<int>(typed->schema().size());
        if (fieldIndex < 0 || fieldIndex >= schemaSize)
            throw sol::error(
                "getLuaField: fieldIndex " + std::to_string(fieldIndex) + " out of range [0, " +
                std::to_string(schemaSize) + ")"
            );
        return typed->readFieldAt(row, fieldIndex, m_lua);
    };

    m_lua["IREntity"]["setLuaField"] = [this](
                                           IRScript::LuaEntity entity,
                                           sol::table componentDef,
                                           int fieldIndex,
                                           sol::object value
                                       ) {
        const IREntity::ComponentId componentId = componentDef.get<lua_Integer>("componentId");
        auto &em = IREntity::getEntityManager();
        auto [data, row] = em.getComponentDataAndRow(entity.entity, componentId);
        if (!data)
            return;
        auto *typed = static_cast<IComponentDataLuaTyped *>(data);
        const int schemaSize = static_cast<int>(typed->schema().size());
        if (fieldIndex < 0 || fieldIndex >= schemaSize)
            throw sol::error(
                "setLuaField: fieldIndex " + std::to_string(fieldIndex) + " out of range [0, " +
                std::to_string(schemaSize) + ")"
            );
        typed->writeFieldAt(row, fieldIndex, value);
    };

    bindLuaDrivenSystems();

    // T-102: pipeline composition + enum bindings + modifier-framework bindings.
    // After bindLuaDrivenSystems() so the IRSystem table already exists;
    // these calls extend it with `SystemName`, `systemId`, and
    // `registerPipeline`. `bindIRTimeEvents` populates `IRTime`;
    // `bindModifierFramework` populates `IRModifier`.
    detail::bindIRTimeEvents(*this);
    detail::bindSystemNameEnum(*this);
    detail::bindConcurrencyEnum(*this);
    detail::bindRegisterPipelineAndSystemId(*this, prefabSystemIds());
    detail::bindModifierFramework(*this);
    detail::bindPrefabApi(*this);
    detail::bindSpatialApi(*this);
    detail::bindRenderGlue(*this);
    detail::bindWidgets(*this);
    detail::bindCollisionEvents(*this);
    detail::bindSimApi(*this);
    detail::bindAudioApi(*this);
    detail::bindPersistenceApi(*this);
    detail::bindWorldSnapshotApi(*this);
}

void LuaScript::bindLuaCommands() {
    detail::bindCommandNameEnum(*this);
    detail::bindInputEnums(*this);
    detail::bindCommandFunctions(*this);
}

void LuaScript::bindLuaInput() {
    detail::bindKeyMouseButtonEnum(*this);
    detail::bindButtonStatusesEnum(*this);
    detail::bindSyntheticInput(*this);
}

namespace {

// Resolve one entry of a `components` / `excludes` list to a
// `ComponentId`. Lists may hold either:
//   - a string (the Lua name of a C++ component bound via
//     `lua_component_pack`, e.g. "C_LocalTransform", or the user name of a
//     Lua-defined component, e.g. "Hp"); OR
//   - a table handle returned by `IRComponent.register` (as produced
//     by T-100, holding `componentId` + `typeName` + `fields`).
//
// Returns `kNullComponent` and sets `errorMessage` when the entry is
// neither (caller surfaces a Lua error so the user sees an actionable
// "this name isn't bound" message — the same "fails fast on unbound
// C++ type" requirement from the T-101 plan).
IREntity::ComponentId resolveComponentEntry(
    const sol::object &entry,
    const LuaScript &script,
    const IREntity::EntityManager &em,
    std::string &errorMessage
) {
    if (entry.is<sol::table>()) {
        sol::table t = entry.as<sol::table>();
        sol::optional<lua_Integer> id = t.get<sol::optional<lua_Integer>>("componentId");
        if (id && *id != 0) {
            return static_cast<IREntity::ComponentId>(*id);
        }
        errorMessage = "table entry is missing componentId — pass either a "
                       "string name or an IRComponent.register handle";
        return IREntity::kNullComponent;
    }
    if (entry.is<std::string>()) {
        const std::string name = entry.as<std::string>();
        // C++ components register their Lua-visible name through
        // `LuaScript::registerType`. Lua-defined components register
        // their user name through `EntityManager::registerComponentDynamic`.
        IREntity::ComponentId fromCpp = script.componentIdByLuaName(name);
        if (fromCpp != IREntity::kNullComponent) {
            return fromCpp;
        }
        IREntity::ComponentId fromLuaTyped = em.getComponentTypeByName(name);
        if (fromLuaTyped != IREntity::kNullComponent) {
            return fromLuaTyped;
        }
        errorMessage = "unknown component '" + name +
                       "' — must be a Lua-defined component (IRComponent.register) "
                       "or a C++ component included in this creation's lua_component_pack";
        return IREntity::kNullComponent;
    }
    errorMessage = "component list entry must be a string name or an "
                   "IRComponent.register handle";
    return IREntity::kNullComponent;
}

// Build the std::vector<ComponentId> for a `components` / `excludes`
// list. Throws sol::error on the first unresolvable entry.
std::vector<IREntity::ComponentId> resolveComponentList(
    const sol::table &list,
    const std::string &fieldName,
    const std::string &systemName,
    const LuaScript &script,
    const IREntity::EntityManager &em
) {
    std::vector<IREntity::ComponentId> ids;
    ids.reserve(list.size());
    for (auto &kv : list) {
        std::string err;
        IREntity::ComponentId id = resolveComponentEntry(kv.second, script, em, err);
        if (id == IREntity::kNullComponent) {
            throw sol::error{
                "IRSystem.registerSystem: '" + systemName + "'." + fieldName + ": " + err
            };
        }
        ids.push_back(id);
    }
    return ids;
}

} // namespace

void LuaScript::bindLuaDrivenSystems() {
    if (m_lua["IRSystem"].valid()) {
        return;
    }

    // Column-view usertypes shared by every Lua-driven system tick.
    // Registered once; `registerSystem` plugs concrete column pointers
    // into fresh views per archetype invocation.
    m_lua.new_usertype<LuaCppColumnView>(
        "_IRLuaCppColumnView",
        sol::no_constructor,
        "at",
        &LuaCppColumnView::at,
        "setAt",
        &LuaCppColumnView::setAt,
        "length",
        &LuaCppColumnView::length
    );
    m_lua.new_usertype<LuaTypedColumnView>(
        "_IRLuaTypedColumnView",
        sol::no_constructor,
        "getField",
        &LuaTypedColumnView::getField,
        "setField",
        &LuaTypedColumnView::setField,
        "getRow",
        &LuaTypedColumnView::getRow,
        "setRow",
        &LuaTypedColumnView::setRow,
        "length",
        &LuaTypedColumnView::length
    );

    m_lua["IRSystem"] = m_lua.create_table();

    auto registerSystem = [this](sol::table args) -> sol::object {
        sol::optional<std::string> nameOpt = args.get<sol::optional<std::string>>("name");
        if (!nameOpt) {
            throw sol::error{"IRSystem.registerSystem: missing required field 'name'"};
        }
        const std::string systemName = *nameOpt;

        // Per-system mode override + creation default. Codegen-bound
        // systems (`mode = "codegen"`, or absent under a CODEGEN creation
        // default) are deliberate no-ops at runtime — the codegen-emitted
        // `createSystem_<NAME>()` already created the system and the
        // runtime call exists only so the same .lua file can drive both
        // build-time codegen and runtime EVAL registration without
        // diverging. Returning a 0 SystemId here matches what the codegen-
        // tool's IRSystem shim returns for the build-time pass — Lua code
        // that captures the result for pipeline registration must pull the
        // CODEGEN id from the C++-side `CodegenSystemIds` instead.
        sol::optional<std::string> modeOpt = args.get<sol::optional<std::string>>("mode");
        EcsMode resolvedMode = m_ecsDefaultMode;
        if (modeOpt) {
            if (*modeOpt == "codegen") {
                resolvedMode = EcsMode::CODEGEN;
            } else if (*modeOpt == "eval") {
                resolvedMode = EcsMode::EVAL;
            } else {
                throw sol::error{
                    "IRSystem.registerSystem: '" + systemName + "' has unknown mode '" + *modeOpt +
                    "'. Allowed: \"codegen\", \"eval\" (or omit for creation default)."
                };
            }
        }
        if (resolvedMode == EcsMode::CODEGEN) {
            return sol::make_object(m_lua, static_cast<lua_Integer>(0));
        }

        sol::optional<sol::table> components = args.get<sol::optional<sol::table>>("components");
        if (!components) {
            throw sol::error{
                "IRSystem.registerSystem: '" + systemName +
                "' missing required field 'components' (list of component names or handles)"
            };
        }

        sol::optional<sol::function> tickOpt = args.get<sol::optional<sol::function>>("tick");
        if (!tickOpt) {
            throw sol::error{
                "IRSystem.registerSystem: '" + systemName + "' missing required field 'tick'"
            };
        }
        // T-103: stash the tick in a shared_ptr so `IRSystem.replaceSystemBody`
        // can later reseat the underlying sol::protected_function. The body
        // lambda below captures `tickRef` (the shared_ptr); the registered-
        // system map (`m_luaSystemTicks[systemId] = tickRef`) keeps an
        // independent ref so `IRSystem.replaceSystemBody` can locate and
        // reseat the function by `SystemId`, and so `~LuaScript()` can
        // explicitly release the ref while the Lua state is still alive.
        auto tickRef = std::make_shared<sol::protected_function>(std::move(*tickOpt));

        auto &em = IREntity::getEntityManager();

        std::vector<IREntity::ComponentId> includeIds =
            resolveComponentList(*components, "components", systemName, *this, em);
        std::vector<std::string> includeNames;
        includeNames.reserve(includeIds.size());
        for (auto &kv : *components) {
            includeNames.push_back(
                kv.second.is<std::string>()
                    ? kv.second.as<std::string>()
                    : kv.second.as<sol::table>().get<std::string>("typeName")
            );
        }

        std::vector<IREntity::ComponentId> excludeIds;
        sol::optional<sol::table> excludes = args.get<sol::optional<sol::table>>("excludes");
        if (excludes) {
            excludeIds = resolveComponentList(*excludes, "excludes", systemName, *this, em);
        }

        // T-223: optional `concurrency` field. Accept the integer-typed
        // `IRSystem.Concurrency.{SERIAL,PARALLEL_FOR,MAIN_THREAD}` table
        // entry only; string names are rejected per the cpp-lua-enums
        // rule. PARALLEL_FOR is structurally unsafe for EVAL — the body
        // is a sol::protected_function call and both sol2 and LuaJIT's
        // GC are single-threaded — so PARALLEL_FOR is forced to
        // MAIN_THREAD with a one-time per-system warning so the misuse
        // surfaces in the log. MAIN_THREAD is the explicit "do not pull
        // me onto a worker" tag for pipeline groups (T-224); SERIAL is
        // the legacy default.
        IRSystem::Concurrency concurrency = IRSystem::Concurrency::SERIAL;
        sol::object concObj = args["concurrency"];
        if (concObj.valid() && concObj.get_type() != sol::type::lua_nil) {
            if (concObj.get_type() == sol::type::string) {
                throw sol::error{
                    "IRSystem.registerSystem: '" + systemName +
                    "' field 'concurrency' must be an IRSystem.Concurrency.* "
                    "value (e.g. IRSystem.Concurrency.MAIN_THREAD), not a string"
                };
            }
            if (!concObj.is<lua_Integer>()) {
                throw sol::error{
                    "IRSystem.registerSystem: '" + systemName +
                    "' field 'concurrency' must be an integer-typed "
                    "IRSystem.Concurrency.* value"
                };
            }
            const lua_Integer raw = concObj.as<lua_Integer>();
            if (raw < static_cast<lua_Integer>(IRSystem::Concurrency::SERIAL) ||
                raw > static_cast<lua_Integer>(IRSystem::Concurrency::MAIN_THREAD)) {
                throw sol::error{
                    "IRSystem.registerSystem: '" + systemName +
                    "' field 'concurrency' = " + std::to_string(raw) +
                    " is out of range; use IRSystem.Concurrency.{SERIAL,"
                    "PARALLEL_FOR,MAIN_THREAD}"
                };
            }
            concurrency = static_cast<IRSystem::Concurrency>(raw);
            if (concurrency == IRSystem::Concurrency::PARALLEL_FOR) {
                if (m_warnedParallelForEvalSystems.insert(systemName).second) {
                    IRE_LOG_WARN(
                        "Lua EVAL system '{}' requested "
                        "Concurrency::PARALLEL_FOR but EVAL bodies are "
                        "sol::protected_function calls into LuaJIT — "
                        "both sol2 and LuaJIT GC are single-threaded. "
                        "Forcing Concurrency::MAIN_THREAD. When codegen "
                        "lifts the batch-form restriction, switch to "
                        "mode='codegen' for native-speed parallel "
                        "dispatch (tracked in #1120).",
                        systemName.c_str()
                    );
                }
                concurrency = IRSystem::Concurrency::MAIN_THREAD;
            }
        }

        IREntity::Archetype includeArchetype{includeIds.begin(), includeIds.end()};
        IREntity::Archetype excludeArchetype{excludeIds.begin(), excludeIds.end()};

        // Body wrapper: one sol::function invocation per matched
        // archetype per tick. Column views are stack-allocated value
        // types pushed into a fresh per-tick `archetype` table; the
        // body inside Lua iterates rows itself, so per-entity work
        // never crosses the C++/Lua boundary except through column
        // userdata methods (sol2 method calls, not sol::function
        // invocations).
        auto body =
            [this, tickRef, includeIds, includeNames, systemName](IREntity::ArchetypeNode *node) {
                sol::state_view lua{m_lua.lua_state()};
                sol::table archView = lua.create_table();
                archView["length"] = node->length_;
                // entity-id access without copying node->entities_ (which
                // would be O(N) per archetype tick at large entity counts).
                archView["entityAt"] = [node](int row) -> lua_Integer {
                    return static_cast<lua_Integer>(node->entities_[row]);
                };

                for (std::size_t i = 0; i < includeIds.size(); ++i) {
                    const IREntity::ComponentId cid = includeIds[i];
                    IREntity::IComponentData *impl = node->components_.at(cid).get();
                    if (auto *typed = dynamic_cast<IComponentDataLuaTyped *>(impl)) {
                        archView[includeNames[i]] = LuaTypedColumnView{typed, node->length_};
                        continue;
                    }
                    const LuaCppColumnAccessor *accessor = cppColumnAccessor(cid);
                    IR_ASSERT(
                        accessor != nullptr,
                        "Lua system '{}': component id {} has no Lua binding accessor — was it "
                        "registered via LuaScript::registerType?",
                        systemName.c_str(),
                        cid
                    );
                    archView[includeNames[i]] = LuaCppColumnView{impl, accessor, node->length_};
                }

                sol::protected_function_result result = (*tickRef)(archView);
                if (!result.valid()) {
                    sol::error err = result;
                    IRE_LOG_ERROR("Lua system '{}' tick error: {}", systemName.c_str(), err.what());
                }
            };

        IRSystem::SystemId systemId = IRSystem::createSystemDynamic(
            systemName,
            std::move(includeArchetype),
            std::move(excludeArchetype),
            std::move(body),
            concurrency
        );
        m_luaSystemTicks.emplace(systemId, tickRef);
        return sol::make_object(m_lua, static_cast<lua_Integer>(systemId));
    };

    m_lua["IRSystem"]["registerSystem"] = registerSystem;

    // T-103: hot-reload the tick body of a previously-registered Lua
    // system. Reseats the captured sol::protected_function inside the
    // shared_ptr that the registerSystem body lambda holds; the next
    // pipeline tick on `systemId` invokes `newTick`. SystemId, archetype
    // filter, exclude archetype, and pipeline registrations are
    // unchanged. Only systems registered via `IRSystem.registerSystem`
    // are eligible (C++/prefab system ids raise a Lua error). See
    // engine/script/CLAUDE.md and docs/design/lua-driven-ecs.md.
    auto replaceSystemBody = [this](lua_Integer systemIdLua, sol::protected_function newTick) {
        if (!newTick.valid()) {
            throw sol::error{"IRSystem.replaceSystemBody: 'newTick' must be a function"};
        }
        const auto systemId = static_cast<IRSystem::SystemId>(systemIdLua);
        auto it = m_luaSystemTicks.find(systemId);
        if (it == m_luaSystemTicks.end()) {
            // Hot-reload is an EVAL-only feature. CODEGEN systems and
            // prefab systems are static at build time; the
            // `m_luaSystemTicks` miss covers both, but the error string
            // names CODEGEN explicitly so the dev sees the next step
            // (mark the system `mode = "eval"` or rebuild).
            throw sol::error{
                "IRSystem.replaceSystemBody: hot-reload not supported for SystemId " +
                std::to_string(systemIdLua) +
                " — only systems registered via IRSystem.registerSystem with "
                "mode='eval' (or under an EVAL creation default) support hot-reload. "
                "CODEGEN systems and prefab systems are static at build time; "
                "mark the system mode='eval' or rebuild the binary."
            };
        }
        *it->second = std::move(newTick);
    };
    m_lua["IRSystem"]["replaceSystemBody"] = replaceSystemBody;
}

} // namespace IRScript
