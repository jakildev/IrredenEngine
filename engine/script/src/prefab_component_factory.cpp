#include <irreden/script/prefab_component_factory.hpp>

#include <unordered_map>

namespace IRPrefab::Prefab {

namespace {

// Process-singleton registry. Mirrors the lifetime of the prefab path registry
// in prefab_api.cpp — populated once per creation init (each component's
// `*_lua.hpp` opts in from its `bindLuaType`), cleared by tests via
// `clearComponentFactories()`.
std::unordered_map<std::string, ComponentFactory> &registry() {
    static std::unordered_map<std::string, ComponentFactory> g_registry;
    return g_registry;
}

} // namespace

void registerComponentFactory(std::string componentName, ComponentFactory factory) {
    registry()[std::move(componentName)] = std::move(factory);
}

const ComponentFactory *findComponentFactory(std::string_view componentName) {
    auto &reg = registry();
    auto it = reg.find(std::string{componentName});
    if (it == reg.end()) {
        return nullptr;
    }
    return &it->second;
}

void clearComponentFactories() {
    registry().clear();
}

} // namespace IRPrefab::Prefab
