#ifndef SYSTEM_MODIFIER_RESOLVE_LAMBDA_H
#define SYSTEM_MODIFIER_RESOLVE_LAMBDA_H

// Lambda escape hatch: applies LambdaModifier::fn_ on top of whatever
// the structured-resolver pipeline produced. Each lambda transforms the
// already-resolved value for its target field. Lambdas with no matching
// resolved field are silently ignored — registering a field is the
// caller's responsibility.

#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_modifiers.hpp>

namespace IRSystem {

template <> struct System<MODIFIER_RESOLVE_LAMBDA> {
    static SystemId create() {
        return createSystem<
            IRComponents::C_LambdaModifiers,
            IRComponents::C_ResolvedFields
        >(
            "ModifierResolveLambda",
            [](IRComponents::C_LambdaModifiers &m,
               IRComponents::C_ResolvedFields &resolved) {
                for (const auto &lambda : m.modifiers_) {
                    if (!lambda.fn_) continue;
                    for (auto &rf : resolved.fields_) {
                        if (rf.field_ == lambda.field_) {
                            rf.value_ = lambda.fn_(rf.value_);
                        }
                    }
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_MODIFIER_RESOLVE_LAMBDA_H */
