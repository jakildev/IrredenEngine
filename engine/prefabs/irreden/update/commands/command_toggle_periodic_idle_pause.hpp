#ifndef COMMAND_TOGGLE_PERIODIC_IDLE_PAUSE_H
#define COMMAND_TOGGLE_PERIODIC_IDLE_PAUSE_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/update/components/component_periodic_idle.hpp>

namespace IRCommand {

template <> struct Command<TOGGLE_PERIODIC_IDLE_PAUSE> {
    static constexpr float kResumeStaggerStepSec = 0.005f;

    static auto create() {
        return []() {
            static bool wantPaused = false;
            wantPaused = !wantPaused;

            auto nodes = IREntity::queryArchetypeNodesSimple(
                IREntity::getArchetype<IRComponents::C_PeriodicIdle>()
            );

            if (wantPaused) {
                for (auto *node : nodes) {
                    auto &idles =
                        IREntity::getComponentData<IRComponents::C_PeriodicIdle>(node);
                    for (auto &idle : idles) {
                        idle.requestPauseAtCycleStart();
                    }
                }
            } else {
                int totalCount = 0;
                for (auto *node : nodes) {
                    totalCount += static_cast<int>(
                        IREntity::getComponentData<IRComponents::C_PeriodicIdle>(node).size()
                    );
                }
                int index = 0;
                for (auto *node : nodes) {
                    auto &idles =
                        IREntity::getComponentData<IRComponents::C_PeriodicIdle>(node);
                    for (auto &idle : idles) {
                        float delay =
                            static_cast<float>(totalCount - 1 - index) * kResumeStaggerStepSec;
                        idle.resumeWithDelay(delay);
                        index++;
                    }
                }
            }
        };
    }
};

} // namespace IRCommand

#endif /* COMMAND_TOGGLE_PERIODIC_IDLE_PAUSE_H */
