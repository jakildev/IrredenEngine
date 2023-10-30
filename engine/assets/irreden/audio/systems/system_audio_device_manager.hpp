/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_systems\system_audio_device_manager.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Work in progress, this stuff might be moved somewhere else

#ifndef SYSTEM_AUDIO_DEVICE_MANAGER_H
#define SYSTEM_AUDIO_DEVICE_MANAGER_H

#include "..\entity\ir_system_base.hpp"

#include "..\entities\entity_midi_device.hpp"

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {

    template<>
    class IRSystem<system_name> : public IRSystemBase<
        system_name,
        components...
    >   {
    public:
        IRSystem()
        {
            IRProfile::engLogInfo("Created system ");
        }
        void tickWithArchetype(
            Archetype type,
            std::vector<EntityId>& entities
        )
        {
            for(int i=0; i < entities.size(); i++) {
                IRProfile::profileFunction(IR_PROFILER_COLOR_RENDER);
            }
        }

        void initAudioDevices() {
            std::vector<C_AudioDeviceIn> audioDeviceIns;
            std::vector<C_AudioDeviceOut> audioDeviceOuts;

            template <typename... Components>
            tickWithArchetypeStandalone(
                auto nodes =
                    IRECS::getEntityManager().
                    getArchetypeGraph()->
                    queryArchetypeNodesSimple(
                        IRECS::getEntityManager().getArchetype<Components...>()
                    );
            )

            // this can be first experiment with unorthodox
            // tickWithArchetype call
        }

    private:
        virtual void beginExecute() override {
            IRProfile::profileFunction(IR_PROFILER_COLOR_RENDER);
        }

        virtual void endExecute() override {
            IRProfile::profileFunction(IR_PROFILER_COLOR_RENDER);
        }


    };


} // namespace IRSystem

#endif /* SYSTEM_AUDIO_DEVICE_MANAGER_H */
