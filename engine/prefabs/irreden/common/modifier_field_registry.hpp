#ifndef MODIFIER_FIELD_REGISTRY_H
#define MODIFIER_FIELD_REGISTRY_H

// Init-time field-binding registry for the modifier framework.
// FieldBindingId is a dense integer; index 0 is reserved for
// kInvalidFieldId so default-constructed Modifier{} cannot resolve.
// Registration is single-threaded (init phase). Lookups are O(1).
//
// See docs/design/modifiers.md §Public API surface.

#include <irreden/common/components/component_modifiers.hpp>

#include <cstddef>
#include <cstring>
#include <vector>

namespace IRPrefab::Modifier::detail {

class FieldRegistry {
  public:
    FieldRegistry() {
        m_names.reserve(64);
        m_names.push_back(nullptr); // index 0 reserved (kInvalidFieldId)
    }

    IRComponents::FieldBindingId registerField(const char *name) {
        const auto id = static_cast<IRComponents::FieldBindingId>(m_names.size());
        m_names.push_back(name);
        return id;
    }

    const char *fieldName(IRComponents::FieldBindingId id) const {
        if (id == IRComponents::kInvalidFieldId || id >= m_names.size())
            return nullptr;
        return m_names[id];
    }

    // Linear scan over registered names. v1 field counts are small
    // (~tens) so a hash-by-name is unjustified; if a creation registers
    // hundreds of fields, swap this for an unordered_map<string_view,
    // FieldBindingId>. The Lua bindings call this per `IRModifier.add`
    // when the caller passes a field name string instead of a numeric
    // binding id, and the registered names use static-storage lifetime
    // (string literals or names() back-buffer in lua_script.cpp), so
    // pointer comparisons would miss equal-but-distinct strings — strcmp
    // is the correct semantic.
    IRComponents::FieldBindingId findFieldId(const char *name) const {
        if (name == nullptr)
            return IRComponents::kInvalidFieldId;
        for (std::size_t i = 1; i < m_names.size(); ++i) {
            if (m_names[i] != nullptr && std::strcmp(m_names[i], name) == 0) {
                return static_cast<IRComponents::FieldBindingId>(i);
            }
        }
        return IRComponents::kInvalidFieldId;
    }

    std::size_t fieldCount() const {
        return m_names.size() - 1;
    }

  private:
    std::vector<const char *> m_names;
};

inline FieldRegistry &globalFieldRegistry() {
    static FieldRegistry registry;
    return registry;
}

} // namespace IRPrefab::Modifier::detail

#endif /* MODIFIER_FIELD_REGISTRY_H */
