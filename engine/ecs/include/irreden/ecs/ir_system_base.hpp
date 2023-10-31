/*
 * Project: Irreden Engine
 * File: ir_system_base.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_SYSTEM_BASE_H
#define IR_SYSTEM_BASE_H

#include <irreden/ir_input.hpp>
#include "ir_system_virtual.hpp"
#include <irreden/ecs/entity_manager.hpp>
#include <irreden/ir_profile.hpp>
// #include <irreden/ir_command.hpp>
#include "entity_handle.hpp"
#include "system_manager.hpp"

// #include <irreden/command/command_manager.hpp>

#include <irreden/common/components/component_tags_all.hpp>

using namespace IRInput;
// using namespace IRCommands;

namespace IRECS {

    // I want to try the "deducing this" pattern to solve all
    // my system problems.
    template <IRSystemName System, typename... Components>
    class IRSystemBase : public IRSystemVirtual {
    public:
        IRSystemBase()
        :   IRSystemVirtual{
                IRECS::getEntityManager().getArchetype<Components...>(),
                System
            }
        {
            IRProfile::engLogInfo(
                "Created system with archetype {}",
                IRECS::makeComponentString(this->getArchetype())
            );
        }
        virtual ~IRSystemBase() = default;

        // What if instead of one call per archetype node it was a span
        // of all components this frame...
        virtual void tick(ArchetypeNode* node) override {
            std::stringstream ss;
            ss << "IRSystemBase::tick " << static_cast<int>(System);
            IRProfile::profileBlock(ss.str().c_str(), IR_PROFILER_COLOR_UPDATE);

            // TODO: This is not intuative and should change or
            // be enforced somewhere else.
            if(node->type_.size() <= 1) {
                // Skip entities with only one component for now
                return;
            }
            if(!std::includes(
                node->type_.begin(),
                node->type_.end(),
                m_includeTags.begin(),
                m_includeTags.end())
            )
            {
                // Skip entities that don't have the required tags
                return;
            }
            auto paramTuple = std::make_tuple(
                node->type_,
                std::ref(node->entities_),
                std::ref(IRECS::getEntityManager().getComponentData<Components>(node))...
            );

            std::apply([&](auto&&... args) {
                static_cast<IRSystem<System>*>(this)->
                    tickWithArchetype(
                        std::forward<decltype(args)>(args)...
                    );
                },
                paramTuple
            );
        }

        // CHAT GPT STUFF BUT IS THIS THE SECRET???
        // Iterate over the combined component data
    // template <typename Func>
    // void forEachComponent(Func func) const {
    //     char* data = reinterpret_cast<char*>(std::get<0>(componentData).data());
    //     size_t offset = 0;

    //     for (size_t i = 0; i < sizeof...(Components); ++i) {
    //         size_t componentSize = componentSizes[i];
    //         size_t componentCount = componentCounts[i];

    //         for (size_t j = 0; j < componentCount; ++j) {
    //             func(reinterpret_cast<void*>(data + offset));
    //             offset += componentSize;
    //         }
    //     }
    // }


    // LIKE THIS
    // void tickWithArchetype(const ComponentContainer<C_MidiSequence, C_MidiDevice>& archetypeData) {
    // archetypeData.forEachComponent([](void* component) {
    //     // Process the component here
    // });
    // }

        // TODO One contiguous array
        // void forEachEntity()
    protected:
        // template <IRInputTypes inputType>
        // void registerCommand(
        //     int button,
        //     std::function<void()> command
        // )
        // {
        //     global.commandManager_->registerSystemCommand<System, inputType>(
        //         button,
        //         command
        //     );
        // }

        // template <
        //     IRCommandNames commandName,
        //     IRInputTypes inputType,
        //     typename Function,
        //     typename... Args
        // >
        // void registerCommand(
        //     int button,
        //     Function func,
        //     Args... fixedArgs
        // )
        // {
        //     global.commandManager_->registerEntityCommand<System, commandName, inputType>(
        //         button,
        //         func,
        //         fixedArgs...
        //     );
        // }

        template <typename ComponentTag>
        void addTag() {
            m_includeTags.insert(
                IRECS::getEntityManager().getComponentType<ComponentTag>()
            );
        }

        template <typename... StandaloneComponents>
        void tickWithArchetypeStandalone() {
            auto nodes =
                IRECS::getEntityManager().
                    getArchetypeGraph()->
                    queryArchetypeNodesSimple(
                        IRECS::getEntityManager().getArchetype<Components...>()
                    );
        }

    private:
        Archetype m_includeTags;
    };


} // namespace IRECS

#endif /* IR_SYSTEM_BASE_H */
