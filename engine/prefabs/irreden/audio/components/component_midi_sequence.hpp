/*
 * Project: Irreden Engine
 * File: component_midi_sequence.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_MIDI_SEQUENCE_H
#define COMPONENT_MIDI_SEQUENCE_H

// Perhaps this should be moved to a sample implementation
// (it shouldn't be a standard component)

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/audio/components/component_midi_message.hpp>

#include <optional>

using namespace IRMath;

constexpr int kTicksPerWholeNote = 480 * 4;
// a beat should be considered a quarter note

// Note: A lot of stuff was shoved in here to get it working
// but can def use a refactor along with the midi sequence
// system.

namespace IRComponents {

    struct C_MidiSequence {
        float bpm_;
        double tickCount_;
        std::pair<int, int> timeSignature_;
        std::vector<
            std::pair<int, C_MidiMessage>
        > messageSequence_;
        int nextMessageIndex_;
        int lengthMeasures_;
        double lengthMidiTicks_;

        C_MidiSequence(
            float bpm,
            std::pair<int, int> timeSignature,
            int lengthMeasures,
            std::vector<
                std::pair<int, C_MidiMessage>
            > messageSequence = {}
        )
        :   bpm_{bpm}
        ,   m_bps{bpm / 60.0f}
        ,   timeSignature_{timeSignature}
        ,   lengthMeasures_{lengthMeasures}
        ,   messageSequence_{messageSequence}
        ,   nextMessageIndex_{0}
        ,   tickCount_{0}
        ,   lengthMidiTicks_{
                kTicksPerWholeNote *
                (
                    static_cast<double>(timeSignature_.first) /
                    timeSignature_.second
                ) *
                lengthMeasures_
            }
        ,   m_ticksPerMeasure{
                kTicksPerWholeNote *
                (
                    static_cast<double>(timeSignature_.first) /
                    timeSignature_.second
                )
            }
        ,   m_measuresPerSecond{
                m_bps /
                timeSignature_.first
                * (
                    4.0f / timeSignature_.second
                )
            }
        ,   m_ticksPerSecond{
                m_ticksPerMeasure *
                m_measuresPerSecond
            }
        {

        }

        // Default
        C_MidiSequence()
        :   C_MidiSequence{
                120.0f,
                std::pair<int, int>{4, 4},
                1,
                std::vector<
                    std::pair<int, C_MidiMessage>
                >{}
            }
        {

        }

        void tick() {

        }

        bool isFinished() const {
            return tickCount_ >= lengthMidiTicks_;
        }

        std::optional<C_MidiMessage> getNextMessage() {
            if( nextMessageIndex_ < messageSequence_.size() &&
                tickCount_ >= messageSequence_[nextMessageIndex_].first
            )
            {
                return messageSequence_[nextMessageIndex_++].second;
            }
            return std::nullopt;
        }

        double calcMidiTicksPerFrameTick() const {
            double ticksPerSecond = m_ticksPerMeasure * m_measuresPerSecond;
            return ticksPerSecond / IRConstants::kFPS;
        }

        void reset() {
            nextMessageIndex_ = 0;
            tickCount_ -= lengthMidiTicks_;
        }

        // 0.0 is at start of first measure, 0.5 is middle, 1.0 is start
        // of second measure, etc
        void insertNote(
            double start,
            double holdDurationSeconds,
            unsigned char note,
            unsigned char velocity
        )
        {
            int startTick = start * m_ticksPerMeasure;
            int holdDurationTicks = holdDurationSeconds * m_ticksPerSecond;
            IR_ASSERT(
                startTick >= 0.0 &&
                startTick + holdDurationTicks <= lengthMidiTicks_,
                "Attempted to insert note on outside of sequence"
            );

            messageSequence_.push_back({
                startTick,
                C_MidiMessage{
                    kMidiStatus_NOTE_ON,
                    note,
                    velocity
                }
            });
            messageSequence_.push_back({
                startTick + holdDurationTicks,
                C_MidiMessage{
                    kMidiStatus_NOTE_OFF,
                    note,
                    velocity
                }
            });
            sortSequence();
        }

        float getMeasureLengthSeconds() {
            return 1.0f / m_measuresPerSecond;
        }

        float getSequenceLengthSeconds() {
            return lengthMeasures_ / m_measuresPerSecond;
        }
    private:
        float m_bps;
        double m_ticksPerMeasure;
        float m_measuresPerSecond;
        double m_ticksPerSecond;
        void sortSequence() {
            std::sort(
                messageSequence_.begin(),
                messageSequence_.end(),
                [](const auto& a, const auto& b) {
                    return a.first < b.first;
                }
            );
        }
    };

} // namespace IRComponents

#endif /* COMPONENT_MIDI_SEQUENCE_H */
