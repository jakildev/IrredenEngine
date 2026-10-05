#include <irreden/script/prefab_component_factory.hpp>

#include <algorithm>
#include <unordered_map>

namespace IRPrefab::Prefab {

namespace {

struct FactoryEntry {
    ComponentFactory factory_;
    ComponentFieldValidator validator_;
};

// Process-singleton registry. Mirrors the lifetime of the prefab path registry
// in prefab_api.cpp — populated once per creation init (each component's
// `*_lua.hpp` opts in from its `bindLuaType`), cleared by tests via
// `clearComponentFactories()`.
std::unordered_map<std::string, FactoryEntry> &registry() {
    static std::unordered_map<std::string, FactoryEntry> g_registry;
    return g_registry;
}

} // namespace

void registerComponentFactory(
    std::string componentName, ComponentFactory factory, ComponentFieldValidator validator
) {
    registry()[std::move(componentName)] = FactoryEntry{std::move(factory), std::move(validator)};
}

void unregisterComponentFactory(std::string_view componentName) {
    registry().erase(std::string{componentName});
}

const ComponentFactory *findComponentFactory(std::string_view componentName) {
    auto &reg = registry();
    auto it = reg.find(std::string{componentName});
    if (it == reg.end()) {
        return nullptr;
    }
    return &it->second.factory_;
}

std::optional<std::string>
validateComponentFields(std::string_view componentName, const sol::table &fields) {
    auto &reg = registry();
    auto it = reg.find(std::string{componentName});
    if (it == reg.end() || !it->second.validator_) {
        return std::nullopt;
    }
    return it->second.validator_(fields);
}

std::vector<std::string> listComponentFactories() {
    std::vector<std::string> names;
    names.reserve(registry().size());
    for (const auto &[name, entry] : registry()) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

void clearComponentFactories() {
    registry().clear();
}

} // namespace IRPrefab::Prefab
