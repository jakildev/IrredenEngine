#ifndef MODIFIER_FIELD_REGISTRY_H
#define MODIFIER_FIELD_REGISTRY_H

// Init-time field-binding registry for the modifier framework.
// FieldBindingId is a dense integer; index 0 is reserved for
// kInvalidFieldId so default-constructed Modifier{} cannot resolve.
// Registration is single-threaded (init phase). Lookups are O(1).
// The registry also owns the framework's world-wide decay flag.
//
// See docs/design/modifiers.md §Public API surface.

#include <irreden/common/components/component_modifiers.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace IRPrefab::Modifier::detail {

class FieldRegistry {
  public:
    FieldRegistry() {
        m_names.reserve(64);
        m_types.reserve(64);
        m_names.push_back(nullptr); // index 0 reserved (kInvalidFieldId)
        m_types.push_back(IRComponents::FieldValueType::SCALAR);
    }

    IRComponents::FieldBindingId registerField(
        const char *name, IRComponents::FieldValueType type = IRComponents::FieldValueType::SCALAR
    ) {
        const auto existing = findFieldId(name);
        if (existing != IRComponents::kInvalidFieldId)
            return existing;
        const auto id = static_cast<IRComponents::FieldBindingId>(m_names.size());
        m_names.push_back(name);
        m_types.push_back(type);
        return id;
    }

    IRComponents::FieldBindingId registerFieldVec3(const char *name) {
        return registerField(name, IRComponents::FieldValueType::VEC3);
    }

    IRComponents::FieldBindingId registerFieldQuat(const char *name) {
        return registerField(name, IRComponents::FieldValueType::QUAT);
    }

    const char *fieldName(IRComponents::FieldBindingId id) const {
        if (id == IRComponents::kInvalidFieldId || id >= m_names.size())
            return nullptr;
        return m_names[id];
    }

    // Returns SCALAR for unregistered ids. Push paths consult this to
    // route to the correct internal vector (scalar vs vec3); a wrong-type
    // push is a silent no-op rather than a defensive crash since the
    // caller bug is local.
    IRComponents::FieldValueType fieldType(IRComponents::FieldBindingId id) const {
        if (id == IRComponents::kInvalidFieldId || id >= m_types.size())
            return IRComponents::FieldValueType::SCALAR;
        return m_types[id];
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

    // Set while some `C_Modifiers` may hold a modifier whose lifetime is not
    // -1; `MODIFIER_DECAY` skips its rows while it is clear. Every push that
    // gives a per-entity modifier a finite lifetime arms it, from any thread:
    // the load before the store keeps an already-armed flag a shared read.
    // Only a completed decay pass that left no finite lifetime clears it, so
    // a stale set costs one scan and a missed arm loses expiries. Relaxed
    // order suffices: the scheduler's system boundaries order a push before
    // the next decay pass reads the flag.
    void armDecay() {
        if (!m_decayArmed.load(std::memory_order_relaxed)) {
            m_decayArmed.store(true, std::memory_order_relaxed);
        }
    }

    bool decayArmed() const {
        return m_decayArmed.load(std::memory_order_relaxed);
    }

    void clearDecay() {
        m_decayArmed.store(false, std::memory_order_relaxed);
    }

  private:
    std::vector<const char *> m_names;
    std::vector<IRComponents::FieldValueType> m_types;
    std::atomic<bool> m_decayArmed{true};
};

inline FieldRegistry &globalFieldRegistry() {
    static FieldRegistry registry;
    return registry;
}

inline void armDecayFor(std::int32_t ticksRemaining) {
    if (ticksRemaining != -1) {
        globalFieldRegistry().armDecay();
    }
}

} // namespace IRPrefab::Modifier::detail

#endif /* MODIFIER_FIELD_REGISTRY_H */
