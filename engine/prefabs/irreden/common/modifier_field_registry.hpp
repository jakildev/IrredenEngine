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
        if (id == IRComponents::kInvalidFieldId || id >= m_names.size()) return nullptr;
        return m_names[id];
    }

    std::size_t fieldCount() const { return m_names.size() - 1; }

  private:
    std::vector<const char *> m_names;
};

inline FieldRegistry &globalFieldRegistry() {
    static FieldRegistry registry;
    return registry;
}

} // namespace IRPrefab::Modifier::detail

#endif /* MODIFIER_FIELD_REGISTRY_H */
