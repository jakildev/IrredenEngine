// Work in progress, this stuff might be moved somewhere else

#ifndef SYSTEM_AUDIO_DEVICE_MANAGER_H
#define SYSTEM_AUDIO_DEVICE_MANAGER_H

#include <irreden/ir_system.hpp>

#include <irreden/audio/entities/entity_midi_device.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> class System<system_name> : public SystemBase<system_name, components...> {
  public:
    System() {
        IRE_LOG_INFO("Created system ");
    }
    void tickWithArchetype(Archetype type, std::vector<EntityId> &entities) {
        for (int i = 0; i < entities.size(); i++) {
            IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
        }
    }

    void initAudioDevices() {
        std::vector<C_AudioDeviceIn> audioDeviceIns;
        std::vector<C_AudioDeviceOut> audioDeviceOuts;

        template <typename... Components>
        tickWithArchetypeStandalone(
            auto nodes = IRECS::getEntityManager().getArchetypeGraph()->queryArchetypeNodesSimple(
                IRECS::getEntityManager().getArchetype<Components...>());)

        // this can be first experiment with unorthodox
        // tickWithArchetype call
    }

  private:
    virtual void beginExecute() override {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
    }

    virtual void endExecute() override {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
    }
};

} // namespace IRSystem

#endif /* SYSTEM_AUDIO_DEVICE_MANAGER_H */
