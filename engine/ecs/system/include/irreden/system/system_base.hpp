/*
 * Project: Irreden Engine
 * File: ir_system_base.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_BASE_H
#define SYSTEM_BASE_H

#include <irreden/ir_profile.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/system/system_virtual.hpp>
#include <irreden/system/system_manager.hpp>

// #include <irreden/common/components/component_tags_all.hpp>

namespace IRECS {

    template <SystemName system, typename... Components>
    class SystemBase : public SystemVirtual {
    public:
        SystemBase()
        :   SystemVirtual{
                IRECS::getArchetype<Components...>(),
                system
            }
        {
            IRProfile::engLogInfo(
                "Created system with archetype {}",
                makeComponentString(this->getArchetype())
            );
        }
        virtual ~SystemBase() = default;

        // What if instead of one call per archetype node it was a span
        // of all components this frame...
        virtual void tick(ArchetypeNode* node) override {
            std::stringstream ss;
            ss << "SystemBase::tick " << static_cast<int>(system);
            IR_PROFILE_BLOCK(ss.str().c_str(), IR_PROFILER_COLOR_UPDATE);

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
                std::ref(getComponentData<Components>(node))...
            );

            std::apply([&](auto&&... args) {
                static_cast<System<system>*>(this)->
                    tickWithArchetype(
                        std::forward<decltype(args)>(args)...
                    );
                },
                paramTuple
            );
        }

        virtual Relation getRelation() const override {
            return m_relation;
        }

    protected:
        template <typename ComponentTag>
        void addTag() {
            m_includeTags.insert(
                IRECS::getComponentType<ComponentTag>()
            );
        }

        void setRelationType(Relation relation) {
            m_relation = relation;
        }

        template <typename... StandaloneComponents>
        void tickWithArchetypeStandalone() {
            auto nodes =
                IRECS::queryArchetypeNodesSimple(
                    IRECS::getArchetype<Components...>()
                );
        }

    private:
        Archetype m_includeTags;
        Relation m_relation = NONE;
    };


} // namespace IRECS

#endif /* SYSTEM_BASE_H */
