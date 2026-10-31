#ifndef IR_VOXEL_EDITOR_MODULE_SESSION_VIEW_H
#define IR_VOXEL_EDITOR_MODULE_SESSION_VIEW_H

#include <irreden/ir_math.hpp>

#include "component_records.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace IRVoxelEditor {

enum class RecipeParamType { INT, FLOAT };

struct RecipeParam {
    std::string name_;
    RecipeParamType type_ = RecipeParamType::INT;
    float default_ = 0.0f;
    float min_ = 0.0f;
    float max_ = 1.0f;
};

struct RecipeCell {
    IRMath::ivec3 local_ = IRMath::ivec3(0);
    std::optional<IRMath::Color> color_;
};

struct RecipeClassMaskEntry {
    IRMath::ivec3 local_ = IRMath::ivec3(0);
    std::string className_;
};

struct PartRecipeBinding {
    std::string recipe_;
    std::vector<float> values_;
    IRMath::Color fallbackColor_{};
    std::vector<RecipeClassMaskEntry> mask_;
};

// A control point a recipe declares: placed by its `at` function, moved by its
// `drag` function, which may write only the params it names. `params_` indexes
// the recipe's params_.
struct RecipeHandle {
    std::string name_;
    std::vector<std::size_t> params_;
};

struct ModuleRecipe {
    std::string name_;
    std::vector<RecipeParam> params_;
    std::vector<std::string> classes_;
    std::vector<RecipeHandle> handles_;
};

struct ModuleSessionView {
    using Evaluate = std::function<bool(
        std::size_t,
        const std::vector<float> &,
        IRMath::ivec3,
        const std::vector<RecipeClassMaskEntry> &,
        std::vector<RecipeCell> &,
        std::string &
    )>;

    // (recipe, handle, values, size) -> the handle's set-local point.
    using HandleAt = std::function<bool(
        std::size_t,
        std::size_t,
        const std::vector<float> &,
        IRMath::ivec3,
        IRMath::vec3 &,
        std::string &
    )>;
    // (recipe, handle, values, size, dragged cell) -> values with the handle's
    // params replaced by its drag result, rounded and clamped to their ranges.
    using HandleDrag = std::function<bool(
        std::size_t,
        std::size_t,
        const std::vector<float> &,
        IRMath::ivec3,
        IRMath::ivec3,
        std::vector<float> &,
        std::string &
    )>;

    std::vector<ModuleRecipe> recipes_;
    std::vector<ComponentRecord> components_;
    Evaluate evaluate_;
    HandleAt handleAt_;
    HandleDrag handleDrag_;
};

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_MODULE_SESSION_VIEW_H */
