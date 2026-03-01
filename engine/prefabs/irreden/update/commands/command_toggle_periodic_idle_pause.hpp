#ifndef COMMAND_TOGGLE_PERIODIC_IDLE_PAUSE_H
#define COMMAND_TOGGLE_PERIODIC_IDLE_PAUSE_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/update/components/component_periodic_idle.hpp>
#include <irreden/update/components/component_rhythmic_launch.hpp>

namespace IRCommand {

template <> struct Command<TOGGLE_PERIODIC_IDLE_PAUSE> {
    static constexpr float kResumeStaggerStepSec = 0.005f;

    static auto create() {
        return []() {
            bool anyRunning = false;
            IREntity::forEachComponent<IRComponents::C_PeriodicIdle>(
                [&anyRunning](const IRComponents::C_PeriodicIdle &idle) {
                    if (!idle.isPaused() && !idle.isPauseRequested()) {
                        anyRunning = true;
                    }
                }
            );

            if (anyRunning) {
                IREntity::forEachComponent<IRComponents::C_PeriodicIdle>(
                    [](IRComponents::C_PeriodicIdle &idle) { idle.requestPauseAtCycleStart(); }
                );
            } else {
                int totalCount = IREntity::countComponents<IRComponents::C_PeriodicIdle>();
                int index = 0;
                IREntity::forEachComponent<IRComponents::C_PeriodicIdle>(
                    [&index, totalCount](IRComponents::C_PeriodicIdle &idle) {
                        float delay =
                            static_cast<float>(totalCount - 1 - index) * kResumeStaggerStepSec;
                        idle.resumeWithDelay(delay);
                        ++index;
                    }
                );
                IREntity::forEachComponent<IRComponents::C_RhythmicLaunch>(
                    [](IRComponents::C_RhythmicLaunch &launch) {
                        launch.frozen_ = false;
                    }
                );
            }
        };
    }
};

} // namespace IRCommand

#endif /* COMMAND_TOGGLE_PERIODIC_IDLE_PAUSE_H */
