#ifndef COMPONENT_PERIODIC_IDLE_H
#define COMPONENT_PERIODIC_IDLE_H

#include <irreden/ir_math.hpp>

#include <algorithm>
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

    PeriodStage(
        float startAngle,
        float endAngle,
        float startTValue,
        float endTValue,
        IREasingFunctions easingFunction,
        bool isReversed = false
    )
        : startAngle_{startAngle}
        , endAngle_{endAngle}
        , startTValue_{startTValue}
        , endTValue_{endTValue}
        , easingFunction_{easingFunction}
        , isReversed_{isReversed} {}
};

struct C_PeriodicIdle {
    int tickCount_;
    float angle_;
    vec3 amplitude_;
    float periodLengthSeconds_;
    std::vector<PeriodStage> stages_;
    int currentStageIndex_;
    bool cycleCompleted_;
    bool pauseRequested_;
    bool paused_;
    float resumeCountdownSec_;

    C_PeriodicIdle(float amplitude, float periodLengthSeconds, float offset = 0.0f)
        : C_PeriodicIdle{vec3{0.0f, 0.0f, amplitude}, periodLengthSeconds, offset} {}

    C_PeriodicIdle(vec3 amplitude, float periodLengthSeconds, float offset = 0.0f)
        : tickCount_{0}
        , angle_{offset}
        , amplitude_{amplitude}
        , periodLengthSeconds_{periodLengthSeconds}
        , stages_{}
        , currentStageIndex_{0}
        , cycleCompleted_{false}
        , pauseRequested_{false}
        , paused_{false}
        , resumeCountdownSec_{0.0f}
        , m_angleIncrementPerTick{
              (2.0f * static_cast<float>(M_PI)) / periodLengthSeconds_ /
              static_cast<float>(IRConstants::kFPS)
          } {}

    // Default
    C_PeriodicIdle()
        : C_PeriodicIdle{vec3{0.0f, 0.0f, 0.0f}, 0.0f} {}

    vec3 getValue() const {
        return m_currentValue;
    }

    void requestPauseAtCycleStart() {
        pauseRequested_ = true;
    }

    void resume() {
        paused_ = false;
        pauseRequested_ = false;
        resumeCountdownSec_ = 0.0f;
    }

    void resumeWithDelay(float delaySec) {
        if (delaySec <= 0.0f) {
            resume();
        } else {
            pauseRequested_ = false;
            resumeCountdownSec_ = delaySec;
        }
    }

    bool isPaused() const {
        return paused_;
    }
    bool isPauseRequested() const {
        return pauseRequested_;
    }

    void tick() {
        if (paused_) {
            if (resumeCountdownSec_ > 0.0f) {
                resumeCountdownSec_ -= 1.0f / static_cast<float>(IRConstants::kFPS);
                if (resumeCountdownSec_ <= 0.0f) {
                    resumeCountdownSec_ = 0.0f;
                    paused_ = false;
                }
            }
            return;
        }

        cycleCompleted_ = false;
        tickCount_++;
        angle_ += m_angleIncrementPerTick;

        if (angle_ >= 2.0f * static_cast<float>(M_PI)) {
            angle_ -= 2.0f * static_cast<float>(M_PI);
            currentStageIndex_ = 0;
            cycleCompleted_ = true;

            if (pauseRequested_) {
                angle_ = 0.0f;
                paused_ = true;
                pauseRequested_ = false;
                updateValue();
                return;
            }
        }
        currentStageIndex_ = advanceStageIndex(angle_, currentStageIndex_);
        updateValue();
    }

    void updateValue() {
        m_currentValue = evaluateStage(angle_, stages_[currentStageIndex_]);
    }

    // The offset this idle would produce at an arbitrary raw angle, without
    // disturbing the live animation state -- e.g. to bake a traveling wave's
    // phase-0 value before PERIODIC_IDLE's first tick() (see #2332). Wraps
    // into [0, 2*pi) (a raw phase can span many cycles; the stages cover one)
    // then runs the same stage-search + easing tick() does, so the result
    // matches the running animation exactly.
    vec3 valueAtAngle(float angle) const {
        float wrapped = IRMath::wrapAngleTwoPi(angle);
        return evaluateStage(wrapped, stages_[advanceStageIndex(wrapped, 0)]);
    }

    void addStagePeriodRange(
        float startAngle,
        float endAngle,
        float startTValue,
        float endTValue,
        IREasingFunctions easingFunction,
        bool isReversed = false
    ) {
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
            PeriodStage{startAngle, endAngle, startTValue, endTValue, easingFunction, isReversed}
        );
        sortSequence();
    }

    void addStageDurationSeconds(
        float startTime,
        float durationSeconds,
        float startTValue,
        float endTValue,
        IREasingFunctions easingFunction
    ) {
        addStagePeriodRange(
            secondsToRadians(startTime),
            secondsToRadians(durationSeconds + startTime),
            startTValue,
            endTValue,
            easingFunction
        );
    }

    void appendStageFillEnd(float startTValue, float endTValue, IREasingFunctions easingFunction) {
        addStagePeriodRange(
            stages_.back().endAngle_,
            2.0f * static_cast<float>(M_PI),
            startTValue,
            endTValue,
            easingFunction
        );
    }

    void appendStageDurationPeriod(
        float durationPeriod, float startTValue, float endTValue, IREasingFunctions easingFunction
    ) {
        addStagePeriodRange(
            stages_.back().endAngle_,
            stages_.back().endAngle_ + durationPeriod,
            startTValue,
            endTValue,
            easingFunction
        );
    }

    void appendStageDurationSeconds(
        float durationSeconds, float startTValue, float endTValue, IREasingFunctions easingFunction
    ) {
        addStageDurationSeconds(
            radiansToSeconds(stages_.back().endAngle_),
            durationSeconds,
            startTValue,
            endTValue,
            easingFunction
        );
    }

    void setOffsetSeconds(float offsetSeconds) {
        angle_ = secondsToRadians(offsetSeconds);
    }

    void makeReverseLoop() {
        IR_ASSERT(
            stages_.back().endAngle_ <= static_cast<float>(M_PI),
            "Cannot make reverse loop with end angle greater than PI"
        );
        for (auto &stage : stages_) {
            // TEMP
            IREasingFunctions easingFunction = stage.easingFunction_;
            if (easingFunction == IREasingFunctions::kBackEaseOut) {
                easingFunction = IREasingFunctions::kCubicEaseOut;
            }
            addStagePeriodRange(
                2.0f * static_cast<float>(M_PI) - stage.endAngle_,
                2.0f * static_cast<float>(M_PI) - stage.startAngle_,
                stage.startTValue_,
                stage.endTValue_,
                easingFunction,
                true
            );
        }
    }

  private:
    float m_angleIncrementPerTick;
    vec3 m_currentValue = vec3{0.0f, 0.0f, 0.0f};
    vec3 m_previousValue = vec3{0.0f, 0.0f, 0.0f};

    void sortSequence() {
        std::sort(stages_.begin(), stages_.end(), [](const auto &a, const auto &b) {
            return a.startAngle_ < b.startAngle_;
        });
    }

    float secondsToRadians(float seconds) {
        return seconds / periodLengthSeconds_ * 2.0f * static_cast<float>(M_PI);
    }

    float radiansToSeconds(float radians) {
        return radians / (2.0f * static_cast<float>(M_PI)) * periodLengthSeconds_;
    }

    float mapAngleToStageTValue(float angle, const PeriodStage &stage) const {
        float clampedAngle = std::max(stage.startAngle_, std::min(stage.endAngle_, angle));
        float relativePosition =
            (angle - stage.startAngle_) / (stage.endAngle_ - stage.startAngle_);
        relativePosition = std::max(0.0f, std::min(1.0f, relativePosition));
        if (stage.isReversed_) {
            relativePosition = 1.0f - relativePosition;
        }

        return relativePosition;
    }

    // Amplitude-scaled eased offset at wrappedAngle within stage. Pure; shared
    // by updateValue() (stores it) and valueAtAngle() (returns it).
    vec3 evaluateStage(float wrappedAngle, const PeriodStage &stage) const {
        float mappedAngle = mapAngleToStageTValue(wrappedAngle, stage);
        float easedValue = IRMath::mix(
            stage.startTValue_,
            stage.endTValue_,
            kEasingFunctions.at(stage.easingFunction_)(mappedAngle)
        );
        return amplitude_ * easedValue;
    }

    // Forward-only search for the stage whose range contains wrappedAngle,
    // starting at fromIndex (tick() advances from the current stage as angle_
    // climbs; a from-scratch query passes 0). One implementation shared by
    // tick() and valueAtAngle() so the stage model can't drift between them.
    int advanceStageIndex(float wrappedAngle, int fromIndex) const {
        int stageIndex = fromIndex;
        while (stageIndex < static_cast<int>(stages_.size()) - 1 &&
               wrappedAngle >= stages_[stageIndex].endAngle_) {
            ++stageIndex;
        }
        return stageIndex;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_PERIODIC_IDLE_H */
