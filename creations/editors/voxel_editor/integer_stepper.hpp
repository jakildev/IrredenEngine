#ifndef INTEGER_STEPPER_H
#define INTEGER_STEPPER_H

#include <irreden/render/widgets.hpp>
#include <array>

namespace IRVoxelEditor {

constexpr IRMath::ivec2 kIntegerStepSize{18, 18};
constexpr IRMath::ivec2 kIntegerFieldSize{48, 18};
constexpr IRMath::ivec2 integerFieldPos(IRMath::ivec2 start) {
    return start + IRMath::ivec2(22, 0);
}
constexpr IRMath::ivec2 integerStepPos(IRMath::ivec2 start, int direction) {
    return start + IRMath::ivec2(direction > 0 ? 74 : 0, 0);
}

class IntegerStepper {
  public:
    void create(IRMath::ivec2 pos, int value, int minimum, int maximum) {
        using namespace IRPrefab::Widget;
        m_minus =
            makeButton(integerStepPos(pos, -1), kIntegerStepSize, "-", IRRender::TextSize::SMALL);
        m_field = makeNumberField(
            integerFieldPos(pos),
            kIntegerFieldSize,
            static_cast<float>(value),
            static_cast<float>(minimum),
            static_cast<float>(maximum),
            1.0f,
            IRRender::TextSize::SMALL
        );
        m_plus =
            makeButton(integerStepPos(pos, 1), kIntegerStepSize, "+", IRRender::TextSize::SMALL);
    }
    int value() const {
        return static_cast<int>(IRPrefab::Widget::numberFieldValue(m_field));
    }
    void update() {
        using namespace IRPrefab::Widget;
        const int delta =
            static_cast<int>(wasClicked(m_plus)) - static_cast<int>(wasClicked(m_minus));
        if (delta != 0)
            setNumberFieldValue(m_field, static_cast<float>(value() + delta));
    }
    std::array<IREntity::EntityId, 3> widgets() const {
        return {m_minus, m_field, m_plus};
    }

  private:
    IREntity::EntityId m_minus = IREntity::kNullEntity;
    IREntity::EntityId m_field = IREntity::kNullEntity;
    IREntity::EntityId m_plus = IREntity::kNullEntity;
};
} // namespace IRVoxelEditor
#endif
