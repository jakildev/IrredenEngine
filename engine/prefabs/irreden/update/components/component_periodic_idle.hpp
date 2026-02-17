#ifndef COMPONENT_PERIODIC_IDLE_H
#define COMPONENT_PERIODIC_IDLE_H

#include <irreden/ir_math.hpp>
#include <cmath>

using namespace IRMath;

namespace IRComponents {

struct PeriodStage {
    float startAngle_;
    float endAngle_;
    float startTValue_;
    float endTValue_;
    IREasingFunctions easingFunction_;
    bool isReversed_ = false;

    PeriodStage(float startAngle, float endAngle, float startTValue, float endTValue,
                IREasingFunctions easingFunction, bool isReversed = false)
        : startAngle_{startAngle}, endAngle_{endAngle}, startTValue_{startTValue},
          endTValue_{endTValue}, easingFunction_{easingFunction}, isReversed_{isReversed} {}
};

struct C_PeriodicIdle {
    int tickCount_;
    float angle_;
    float amplitude_;
    float periodLengthSeconds_;
    std::vector<PeriodStage> stages_;
    int currentStageIndex_;

    C_PeriodicIdle(float amplitude, float periodLengthSeconds, float offset = 0.0f)
        : tickCount_{0}, angle_{offset}, amplitude_{amplitude},
          periodLengthSeconds_{periodLengthSeconds}, stages_{}, currentStageIndex_{0},
          m_angleIncrementPerTick{(2.0f * static_cast<float>(M_PI)) / periodLengthSeconds_ /
                                  static_cast<float>(IRConstants::kFPS)} {}

    // Default
    C_PeriodicIdle() : C_PeriodicIdle{0.0f, 0.0f} {}

    float getValue() {
        return m_currentValue;
    }

    // This is slow and should probably be redone
    void tick() {
        tickCount_++;
        angle_ += m_angleIncrementPerTick;

        if (angle_ >= 2.0f * static_cast<float>(M_PI)) {
            angle_ -= 2.0f * static_cast<float>(M_PI);
            currentStageIndex_ = 0;
        }
        while (angle_ >= stages_[currentStageIndex_].endAngle_ &&
               currentStageIndex_ < stages_.size() - 1) {
            currentStageIndex_++;
        }
        updateValue();
    }

    void updateValue() {
        PeriodStage &stage = stages_[currentStageIndex_];
        float mappedAngle = mapAngleToStageTValue(angle_, stage);
        m_currentValue = glm::mix(stage.startTValue_ * amplitude_, stage.endTValue_ * amplitude_,
                                  kEasingFunctions.at(stage.easingFunction_)(mappedAngle));
    }

    void addStagePeriodRange(float startAngle, float endAngle, float startTValue, float endTValue,
                             IREasingFunctions easingFunction, bool isReversed = false) {
        // Values gets optimized out and assert crashes in debug mode
        // IRE_LOG_INFO("startAngle: ", startAngle);
        // IR_ASSERT(
        //     startAngle >= 0.0f &&
        //     startAngle <= 2.0f * static_cast<float>(M_PI),
        //     "Start angle is not in range 0-2PI"
        // );
        // IR_ASSERT(
        //     endAngle >= startAngle &&
        //     endAngle <= 2.0f * static_cast<float>(M_PI),
        //     "End angle is not in range startAngle-2PI"
        // );
        stages_.push_back(
            PeriodStage{startAngle, endAngle, startTValue, endTValue, easingFunction, isReversed});
        sortSequence();
    }

    void addStageDurationSeconds(float startTime, float durationSeconds, float startTValue,
                                 float endTValue, IREasingFunctions easingFunction) {
        addStagePeriodRange(secondsToRadians(startTime),
                            secondsToRadians(durationSeconds + startTime), startTValue, endTValue,
                            easingFunction);
    }

    void appendStageFillEnd(float startTValue, float endTValue, IREasingFunctions easingFunction) {
        addStagePeriodRange(stages_.back().endAngle_, 2.0f * static_cast<float>(M_PI), startTValue,
                            endTValue, easingFunction);
    }

    void appendStageDurationPeriod(float durationPeriod, float startTValue, float endTValue,
                                   IREasingFunctions easingFunction) {
        addStagePeriodRange(stages_.back().endAngle_, stages_.back().endAngle_ + durationPeriod,
                            startTValue, endTValue, easingFunction);
    }

    void appendStageDurationSeconds(float durationSeconds, float startTValue, float endTValue,
                                    IREasingFunctions easingFunction) {
        addStageDurationSeconds(radiansToSeconds(stages_.back().endAngle_), durationSeconds,
                                startTValue, endTValue, easingFunction);
    }

    void setOffsetSeconds(float offsetSeconds) {
        angle_ = secondsToRadians(offsetSeconds);
    }

    void makeReverseLoop() {
        IR_ASSERT(stages_.back().endAngle_ <= static_cast<float>(M_PI),
                  "Cannot make reverse loop with end angle greater than PI");
        for (auto &stage : stages_) {
            // TEMP
            IREasingFunctions easingFunction = stage.easingFunction_;
            if (easingFunction == IREasingFunctions::kBackEaseOut) {
                easingFunction = IREasingFunctions::kCubicEaseOut;
            }
            addStagePeriodRange(2.0f * static_cast<float>(M_PI) - stage.endAngle_,
                                2.0f * static_cast<float>(M_PI) - stage.startAngle_,
                                stage.startTValue_, stage.endTValue_, easingFunction, true);
        }
    }

  private:
    float m_angleIncrementPerTick;
    float m_currentValue = 0.0f;
    float m_previousValue = 0.0f;

    void sortSequence() {
        std::sort(stages_.begin(), stages_.end(),
                  [](const auto &a, const auto &b) { return a.startAngle_ < b.startAngle_; });
    }

    float secondsToRadians(float seconds) {
        return seconds / periodLengthSeconds_ * 2.0f * static_cast<float>(M_PI);
    }

    float radiansToSeconds(float radians) {
        return radians / (2.0f * static_cast<float>(M_PI)) * periodLengthSeconds_;
    }

    float mapAngleToStageTValue(float angle, PeriodStage &stage) {
        float clampedAngle = std::max(stage.startAngle_, std::min(stage.endAngle_, angle));
        float relativePosition =
            (angle - stage.startAngle_) / (stage.endAngle_ - stage.startAngle_);
        relativePosition = std::max(0.0f, std::min(1.0f, relativePosition));
        if (stage.isReversed_) {
            relativePosition = 1.0f - relativePosition;
        }

        return relativePosition;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_PERIODIC_IDLE_H */
