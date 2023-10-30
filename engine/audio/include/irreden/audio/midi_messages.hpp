/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\audio\midi_messages.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Notes: This file isnt really used anymore in favor of
// the C_MidiMessage component, but is a good example
// of using a non enum template parameter and specializing
// in c++.

#ifndef MIDI_MESSAGES_H
#define MIDI_MESSAGES_H

#include <irreden/ir_audio.hpp>
#include <irreden/ir_profiling.hpp>

namespace IRAudio {

    template<unsigned char status>
    class MidiMessage;

    template<>
    class MidiMessage<kMidiStatus_NOTE_OFF> {
    public:
        MidiMessage(
            unsigned char channel,
            unsigned char keyNumber,
            unsigned char offVelocity
        )
        :   m_channel(channel),
            m_keyNumber(keyNumber),
            m_offVelocity(offVelocity)
        {
            IRProfile::engAssert(channel <= 0x0F, "Invalid midi channel");
        }
    private:
        unsigned char m_channel;
        unsigned char m_keyNumber;
        unsigned char m_offVelocity;
    };


    template<>
    class MidiMessage<kMidiStatus_NOTE_ON> {
    public:
        MidiMessage(
            unsigned char channel,
            unsigned char keyNumber,
            unsigned char onVelocity
        )
        :   m_channel(channel),
            m_keyNumber(keyNumber),
            m_onVelocity(onVelocity)
        {
            IRProfile::engAssert(channel <= 0x0F, "Invalid midi channel");
        }

        unsigned char getChannel() const {
            return m_channel;
        }
        unsigned char getKeyNumber() const {
            return m_keyNumber;
        }
        unsigned char getOnVelocity() const {
            return m_onVelocity;
        }
    private:
        unsigned char m_channel;
        unsigned char m_keyNumber;
        unsigned char m_onVelocity;
    };

    template<>
    class MidiMessage<kMidiStatus_POLYPHONIC_KEY_PRESSURE> {
    public:
        MidiMessage(
            unsigned char channel,
            unsigned char keyNumber,
            unsigned char pressure
        )
        :   m_channel(channel),
            m_keyNumber(keyNumber),
            m_pressure(pressure)
        {
            IRProfile::engAssert(channel <= 0x0F, "Invalid midi channel");
        }
    private:
        unsigned char m_channel;
        unsigned char m_keyNumber;
        unsigned char m_pressure;
    };

    template<>
    class MidiMessage<kMidiStatus_CONTROL_CHANGE> {
    public:
        MidiMessage(
            unsigned char channel,
            unsigned char controllerNumber,
            unsigned char controllerValue
        )
        :   m_channel(channel),
            m_controllerNumber(controllerNumber),
            m_controllerValue(controllerValue)
        {
            IRProfile::engAssert(channel <= 0x0F, "Invalid midi channel");
        }
    private:
        unsigned char m_channel;
        unsigned char m_controllerNumber;
        unsigned char m_controllerValue;
    };


    template<>
    class MidiMessage<kMidiStatus_PROGRAM_CHANGE> {
    public:
        MidiMessage(
            unsigned char channel,
            unsigned char programNumber
        )
        :   m_channel(channel),
            m_programNumber(programNumber)
        {
            IRProfile::engAssert(channel <= 0x0F, "Invalid midi channel");
        }
    private:
        unsigned char m_channel;
        unsigned char m_programNumber;
    };


    template<>
    class MidiMessage<kMidiStatus_CHANNEL_PRESSURE> {
    public:
        MidiMessage(
            unsigned char channel,
            unsigned char pressureValue
        )
        :   m_channel(channel),
            m_pressureValue(pressureValue)
        {
            IRProfile::engAssert(channel <= 0x0F, "Invalid midi channel");
        }
    private:
        unsigned char m_channel;
        unsigned char m_pressureValue;
    };


    template<>
    class MidiMessage<kMidiStatus_PITCH_BEND> {
    public:
        MidiMessage(
            unsigned char channel,
            unsigned char msbValue,
            unsigned char lsbValue
        )
        :   m_channel(channel),
            m_msbValue(msbValue),
            m_lsbValue(lsbValue)
        {
            IRProfile::engAssert(channel <= 0x0F, "Invalid midi channel");
        }
    private:
        unsigned char m_channel;
        unsigned char m_msbValue;
        unsigned char m_lsbValue;
    };


} // namespace IRAudio

#endif /* MIDI_MESSAGES_H */
