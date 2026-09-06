// #3091 regression surface: the declarations `lua_component_codegen_test.cpp`
// reaches a SECOND translation unit through.
//
// Deliberately does NOT include the generated header — the point of the pairing
// is that `lua_component_codegen_second_tu.cpp` includes it independently of
// the test TU, so the two are genuinely separate includers of one codegen run's
// header. A shared include here would collapse them back into one.
#pragma once

#include <cstddef>
#include <typeinfo>

namespace IRTestCodegenSecondTu {

// `typeid` of the generated `IRComponents::C_CodegenHp`, as this TU sees it.
// Comparing it against the test TU's is what proves both TUs got the same type
// rather than two same-shaped types that merely agree field-by-field.
const std::type_info &hpTypeId();

// A `C_CodegenHp` built here and read back in the test TU, reported field-wise
// so the header this declares stays free of the generated types.
void makeHp(int current, int max, int &outCurrent, int &outMax);

// `sizeof(IRComponents::C_CodegenHp)` as this TU sees it.
std::size_t hpSize();

} // namespace IRTestCodegenSecondTu
