/*
 * Project: Irreden Engine
 * File: ir_system_virtual.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_SYSTEM_VIRTUAL_H
#define IR_SYSTEM_VIRTUAL_H

#include <irreden/ir_entity.hpp>

#include <irreden/system/ir_system_types.hpp>


// Virtual should just be the interface
namespace IRECS {

    class SystemVirtual {
    public:
        SystemVirtual(Archetype type, SystemName name)
        :   m_archetype(type)
        ,   m_name(name)
        {
        }
        virtual ~SystemVirtual() = default;
        virtual void beginExecute() {};
        virtual void endExecute() {};
        virtual void tick(ArchetypeNode* node) {};
        virtual void start() {};
        virtual void end() {};
        // virtual void executeEvent(IREvents event) = 0;
        virtual Relation getRelation() const {
            return Relation::NONE;
        }

        inline const SystemName getSystemName() const {
            return m_name;
        }
        inline const Archetype getArchetype() const {
            return m_archetype;
        }
    private:
        Archetype m_archetype;
        SystemName m_name;
    };

}

#endif /* IR_SYSTEM_VIRTUAL_H */
