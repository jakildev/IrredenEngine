/*
 * Project: Irreden Engine
 * File: ir_command.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_COMMAND_H
#define IR_COMMAND_H

#include <irreden/ecs/entity_handle.hpp>
#include <irreden/ir_ecs.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_audio.hpp>

using namespace IRECS;

namespace IRCommands {

    enum IRCommandNames {
        NULL_COMMAND,
        EXAMPLE,
        ZOOM_IN,
        ZOOM_OUT,
        CLOSE_WINDOW,
        STOP_VELOCITY,
        RESHAPE_SPHERE,
        RESHAPE_RECTANGULAR_PRISM,
        NUM_COMMANDS
    };

    enum IRCommandTypes {
        IR_COMMAND_NULL,
        IR_COMMAND_SYSTEM,
        IR_COMMAND_ENTITY,
        IR_COMMAND_MIDI_NOTE,
        IR_COMMAND_MIDI_CC,
        IR_COMMAND_USER
    };

    template <IRCommandTypes CommandType>
    class IRCommand;

    template <>
    class IRCommand<IR_COMMAND_SYSTEM> {
    public:
        IRCommand(
            IRInput::IRInputTypes type,
            int buttonValue,
            std::function<void()> func
        )
        :   m_type(type)
        ,   m_button(buttonValue)
        ,   m_func(func)
        {

        }

        void execute() const {
            m_func();
        }

        const int getButton() const {
            return m_button;
        }

        const IRInput::IRInputTypes getType() const {
            return m_type;
        }
    private:
        IRInput::IRInputTypes m_type;
        int m_button;
        std::function<void()> m_func;
    };

    template <>
    class IRCommand<IR_COMMAND_ENTITY> {
    public:
        template <typename Function, typename... Args>
        IRCommand(
            IRInput::IRInputTypes type,
            int buttonValue,
            Function func,
            Args... fixedArgs
        )
        :   m_type(type)
        ,   m_button(buttonValue)
        ,   m_func([func, fixedArgs...](EntityHandle entity) {
                func(entity, fixedArgs...);
            })
        {

        }

        void execute(EntityHandle entity) const {
            m_func(entity);
        }

        const int getButton() const {
            return m_button;
        }

        const IRInput::IRInputTypes getType() const {
            return m_type;
        }
    private:
        IRInput::IRInputTypes m_type;
        int m_button;
        std::function<void(EntityHandle)> m_func;
    };

    template <>
    class IRCommand<IR_COMMAND_MIDI_NOTE> {
    public:
        template <typename Function, typename... Args>
        IRCommand(
            IRInput::IRInputTypes type,
            Function func,
            Args... fixedArgs
        )
        :   m_type(type)
        ,   m_func([func, fixedArgs...](
                unsigned char note,
                unsigned char velocity
            )
            {
                func(note, velocity, fixedArgs...);
            })
        {

        }

        void execute(
            unsigned char note,
            unsigned char velocity
        ) const
        {
            m_func(note, velocity);
        }

        const IRInput::IRInputTypes getType() const {
            return m_type;
        }
    private:
        IRInput::IRInputTypes m_type;
        std::function<void(unsigned char, unsigned char)> m_func;
    };

    template <>
    class IRCommand<IR_COMMAND_MIDI_CC> {
    public:
        template <typename Function>
        IRCommand(
            IRInput::IRInputTypes type,
            IRAudio::IRCCMessage ccMessage,
            Function func
        )
        :   m_type(type)
        ,   m_ccMessage(ccMessage)
        ,   m_func([func](
                unsigned char value
            )
            {
                func(value);
            })
        {

        }

        void execute(
            unsigned char value
        ) const
        {
            m_func(value);
        }

        const IRInput::IRInputTypes getType() const {
            return m_type;
        }

        const IRAudio::IRCCMessage getCCMessage() const {
            return m_ccMessage;
        }
    private:
        IRInput::IRInputTypes m_type;
        IRAudio::IRCCMessage m_ccMessage;
        std::function<void(unsigned char)> m_func;
    };

    template <>
    class IRCommand<IR_COMMAND_USER> {
    public:
        template <typename Function>
        IRCommand(
            IRInput::IRInputTypes type,
            int button,
            Function func
        )
        :   m_type(type)
        ,   m_button{button}
        ,   m_func([func]()
            {
                func();
            })
        {

        }

        void execute() const
        {
            m_func();
        }

        const IRInput::IRInputTypes getType() const {
            return m_type;
        }

        const int getButton() const {
            return m_button;
        }

    private:
        IRInput::IRInputTypes m_type;
        int m_button;
        std::function<void()> m_func;
    };

}

#endif /* IR_COMMAND_H */
