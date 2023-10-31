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

#include <irreden/ir_ecs.hpp>
#include <irreden/ecs/archetype_node.hpp>


// Virtual should just be the interface
namespace IRECS {

    class IRSystemVirtual {
    public:
        IRSystemVirtual(Archetype type, IRSystemName name)
        :   m_archetype(type)
        ,   m_name(name)
        {
        }
        virtual ~IRSystemVirtual() = default;
        virtual void beginExecute() {};
        virtual void endExecute() {};
        virtual void tick(ArchetypeNode* node) = 0;
        virtual void start() {};
        virtual void end() {};
        // virtual void executeEvent(IREvents event) = 0;

        inline const IRSystemName getSystemName() const { return m_name; }
        inline const Archetype getArchetype() const { return m_archetype; }
    private:
        Archetype m_archetype;
        IRSystemName m_name;
    };

}

#endif /* IR_SYSTEM_VIRTUAL_H */
