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

struct ModuleRecipe {
    std::string name_;
    std::vector<RecipeParam> params_;
    std::vector<std::string> classes_;
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

    std::vector<ModuleRecipe> recipes_;
    std::vector<ComponentRecord> components_;
    Evaluate evaluate_;
};

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_MODULE_SESSION_VIEW_H */
