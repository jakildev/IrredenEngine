#ifndef LUA_LAYOUT_BINDINGS_H
#define LUA_LAYOUT_BINDINGS_H

#include <irreden/render/components/component_layout_leaf.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/layout.hpp>
#include <irreden/script/ir_script_types.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <stdexcept>
#include <string>
#include <tuple>

namespace IRScript::detail {

inline IRPrefab::Layout::SizeSpec layoutSizeSpec(const sol::table &value) {
    const lua_Integer mode = value.get<lua_Integer>("mode");
    IRPrefab::Layout::SizeMode sizeMode;
    switch (mode) {
    case static_cast<lua_Integer>(IRPrefab::Layout::SizeMode::FIXED_PX):
        sizeMode = IRPrefab::Layout::SizeMode::FIXED_PX;
        break;
    case static_cast<lua_Integer>(IRPrefab::Layout::SizeMode::FRACTION):
        sizeMode = IRPrefab::Layout::SizeMode::FRACTION;
        break;
    default:
        throw std::invalid_argument("IRLayout size mode must use IRLayout.SizeMode");
    }
    const float amount = value.get<float>("value");
    const int minimum = value.get_or("minPx", 0);
    const int maximum = value.get_or("maxPx", 32767);
    if (!(amount > 0.0f) || minimum < 0 || maximum < minimum) {
        throw std::invalid_argument(
            "IRLayout size spec requires value > 0 and 0 <= minPx <= maxPx"
        );
    }
    return {sizeMode, amount, minimum, maximum};
}

inline int layoutParent(const sol::object &value) {
    auto &layout = IRPrefab::Layout::getLayout();
    if (!value.valid() || value.get_type() == sol::type::lua_nil) {
        if (!layout.nodes_.empty()) {
            throw std::invalid_argument("IRLayout accepts nil only for the first, root node");
        }
        return -1;
    }
    if (value.get_type() != sol::type::number) {
        throw std::invalid_argument("IRLayout parent must be nil or a row/column node index");
    }
    const int parent = value.as<int>();
    if (parent < 0 || parent >= static_cast<int>(layout.nodes_.size()) ||
        !IRPrefab::Layout::isContainerNode(layout.nodes_[parent])) {
        throw std::invalid_argument("IRLayout parent must name a live row/column node");
    }
    return parent;
}

inline void requireMutableLayout(const char *function) {
    if (IRPrefab::Layout::getLayout().topologySealed_) {
        throw std::invalid_argument(
            std::string("IRLayout.") + function + " cannot change topology after buildSplitters"
        );
    }
}

inline IREntity::EntityId layoutWidget(const sol::object &value, bool requireUnattached) {
    IREntity::EntityId widget = IREntity::kNullEntity;
    if (value.is<IRScript::LuaEntity>()) {
        widget = value.as<IRScript::LuaEntity>().entity;
    } else if (value.get_type() == sol::type::number) {
        const lua_Integer id = value.as<lua_Integer>();
        if (id > 0) {
            widget = static_cast<IREntity::EntityId>(id);
        }
    }
    if (widget == IREntity::kNullEntity || !IREntity::entityExists(widget) ||
        !IREntity::getComponentOptional<IRComponents::C_Widget>(widget)) {
        throw std::invalid_argument("IRLayout.makeLeaf widget must be a live widget id");
    }
    if (requireUnattached && IREntity::getComponentOptional<IRComponents::C_LayoutLeaf>(widget)) {
        throw std::invalid_argument("IRLayout.makeLeaf widget is already attached to a leaf");
    }
    return widget;
}

inline int requireLayoutNode(int node, const char *function) {
    const auto &nodes = IRPrefab::Layout::getLayout().nodes_;
    if (node < 0 || node >= static_cast<int>(nodes.size())) {
        throw std::invalid_argument(
            std::string("IRLayout.") + function + " node index is out of range"
        );
    }
    return node;
}

inline void bindLayout(LuaScript &script) {
    sol::state &lua = script.lua();
    sol::table layout = lua.create_table();
    sol::table sizeMode = lua.create_table();
#define IR_BIND_LAYOUT_SIZE_MODE(name)                                                             \
    sizeMode[#name] = static_cast<lua_Integer>(IRPrefab::Layout::SizeMode::name)
    IR_BIND_LAYOUT_SIZE_MODE(FIXED_PX);
    IR_BIND_LAYOUT_SIZE_MODE(FRACTION);
#undef IR_BIND_LAYOUT_SIZE_MODE
    layout["SizeMode"] = sizeMode;

    layout["makeRow"] = [](sol::object parent, sol::table spec, sol::optional<std::string> id) {
        requireMutableLayout("makeRow");
        const int parentIndex = layoutParent(parent);
        const auto parsed = layoutSizeSpec(spec);
        return IRPrefab::Layout::makeRow(parentIndex, parsed, id.value_or(""));
    };
    layout["makeColumn"] = [](sol::object parent, sol::table spec, sol::optional<std::string> id) {
        requireMutableLayout("makeColumn");
        const int parentIndex = layoutParent(parent);
        const auto parsed = layoutSizeSpec(spec);
        return IRPrefab::Layout::makeColumn(parentIndex, parsed, id.value_or(""));
    };
    layout["makeLeaf"] =
        [](sol::object parent, sol::table spec, sol::object widget, sol::optional<std::string> id) {
            requireMutableLayout("makeLeaf");
            const int parentIndex = layoutParent(parent);
            const auto parsed = layoutSizeSpec(spec);
            const IREntity::EntityId entity = layoutWidget(widget, true);
            return IRPrefab::Layout::makeLeaf(parentIndex, parsed, entity, id.value_or(""));
        };
    layout["buildSplitters"] = [] {
        auto &state = IRPrefab::Layout::getLayout();
        if (state.topologySealed_) {
            throw std::invalid_argument("IRLayout.buildSplitters may be called only once");
        }
        if (state.nodes_.empty()) {
            throw std::invalid_argument("IRLayout.buildSplitters requires a layout tree");
        }
        IRPrefab::Layout::buildSplitters();
    };
    layout["setRoot"] = [](int node, int x, int y, int width, int height) {
        requireLayoutNode(node, "setRoot");
        if (width <= 0 || height <= 0) {
            throw std::invalid_argument("IRLayout.setRoot requires positive width and height");
        }
        IRPrefab::Layout::setRoot(node, IRMath::ivec2(x, y), IRMath::ivec2(width, height));
    };
    layout["leafRect"] = [](sol::object value) {
        const IREntity::EntityId widget = layoutWidget(value, false);
        if (!IREntity::getComponentOptional<IRComponents::C_LayoutLeaf>(widget)) {
            throw std::invalid_argument("IRLayout.leafRect widget is not attached to a leaf");
        }
        const auto rect = IRPrefab::Layout::leafRect(widget);
        return std::make_tuple(rect.x, rect.y, rect.z, rect.w);
    };
    layout["revision"] = [] { return IRPrefab::Layout::revision(); };
    layout["serialize"] = [] { return IRPrefab::Layout::serialize(); };
    layout["deserialize"] = [](const std::string &value) {
        return IRPrefab::Layout::deserialize(value);
    };
    lua["IRLayout"] = layout;
}

} // namespace IRScript::detail

#endif /* LUA_LAYOUT_BINDINGS_H */
