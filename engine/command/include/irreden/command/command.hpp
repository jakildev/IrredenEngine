/*
 * Project: Irreden Engine
 * File: ir_command.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMMAND_H
#define COMMAND_H

#include <irreden/ir_ecs.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_audio.hpp>

#include <irreden/command/ir_command_types.hpp>

using namespace IRECS;

namespace IRCommand {

    template <CommandTypes CommandType>
    class Command;

    template <>
    class Command<IR_COMMAND_USER> {
    public:
        template <
            typename Function
        >
        Command(
            IRInput::InputTypes type,
            IRInput::ButtonStatuses triggerStatus,
            int button,
            Function func
        )
        :   m_type(type)
        ,   m_button{button}
        ,   m_triggerStatus{triggerStatus}
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

        const IRInput::InputTypes getType() const {
            return m_type;
        }

        const int getButton() const {
            return m_button;
        }

        const IRInput::ButtonStatuses getTriggerStatus() const {
            return m_triggerStatus;
        }

    private:
        IRInput::InputTypes m_type;
        IRInput::ButtonStatuses m_triggerStatus;
        int m_button;
        std::function<void()> m_func;
    };

    // template <>
    // class Command<IR_COMMAND_SYSTEM> {
    // public:
    //     Command(
    //         IRInput::InputTypes type,
    //         int buttonValue,
    //         std::function<void()> func
    //     )
    //     :   m_type(type)
    //     ,   m_button(buttonValue)
    //     ,   m_func(func)
    //     {

    //     }

    //     void execute() const {
    //         m_func();
    //     }

    //     const int getButton() const {
    //         return m_button;
    //     }

    //     const IRInput::InputTypes getType() const {
    //         return m_type;
    //     }
    // private:
    //     IRInput::InputTypes m_type;
    //     int m_button;
    //     std::function<void()> m_func;
    // };

    // template <>
    // class Command<IR_COMMAND_ENTITY> {
    // public:
    //     template <typename Function, typename... Args>
    //     Command(
    //         IRInput::InputTypes type,
    //         int buttonValue,
    //         Function func,
    //         Args... fixedArgs
    //     )
    //     :   m_type(type)
    //     ,   m_button(buttonValue)
    //     ,   m_func([func, fixedArgs...](EntityHandle entity) {
    //             func(entity, fixedArgs...);
    //         })
    //     {

    //     }

    //     void execute(EntityHandle entity) const {
    //         m_func(entity);
    //     }

    //     const int getButton() const {
    //         return m_button;
    //     }

    //     const IRInput::InputTypes getType() const {
    //         return m_type;
    //     }
    // private:
    //     IRInput::InputTypes m_type;
    //     int m_button;
    //     std::function<void(EntityHandle)> m_func;
    // };

    template <>
    class Command<IR_COMMAND_MIDI_NOTE> {
    public:
        template <typename Function, typename... Args>
        Command(
            IRInput::InputTypes type,
            Function onPress,
            // Function onRelease,
            Args... fixedArgs
        )
        :   m_type(type)
        ,   m_func([onPress, fixedArgs...](
                unsigned char note,
                unsigned char velocity
            )
            {
                onPress(note, velocity, fixedArgs...);
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

        const IRInput::InputTypes getType() const {
            return m_type;
        }
    private:
        IRInput::InputTypes m_type;
        std::function<void(unsigned char, unsigned char)> m_func;
    };

    template <>
    class Command<IR_COMMAND_MIDI_CC> {
    public:
        template <typename Function>
        Command(
            IRInput::InputTypes type,
            IRAudio::CCMessage ccMessage,
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

        const IRInput::InputTypes getType() const {
            return m_type;
        }

        const IRAudio::CCMessage getCCMessage() const {
            return m_ccMessage;
        }
    private:
        IRInput::InputTypes m_type;
        IRAudio::CCMessage m_ccMessage;
        std::function<void(unsigned char)> m_func;
    };

} // namespace IRCommand

#endif /* COMMAND_H */
