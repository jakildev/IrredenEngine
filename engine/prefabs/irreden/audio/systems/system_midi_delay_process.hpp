#ifndef SYSTEM_MIDI_DELAY_PROCESS_H
#define SYSTEM_MIDI_DELAY_PROCESS_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/audio/components/component_midi_message.hpp>
#include <irreden/audio/components/component_midi_delay.hpp>
#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/update/components/component_lifetime.hpp>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<MIDI_DELAY_PROCESS> {
    static SystemId create() {
        return createSystem<C_MidiMessage, C_MidiDelay>(
            "MidiDelayProcess", [](C_MidiMessage &msg, C_MidiDelay &delay) {
                if (delay.framesRemaining_ > 0) {
                    delay.framesRemaining_--;
                    return;
                }
                if (delay.framesRemaining_ == 0) {
                    IREntity::createEntity(msg, C_MidiOut{}, C_Lifetime{1});
                    delay.framesRemaining_ = -1;
                }
            });
    }
};

} // namespace IRSystem

#endif /* SYSTEM_MIDI_DELAY_PROCESS_H */
