#ifndef IR_AUDIO_TYPES_H
#define IR_AUDIO_TYPES_H

namespace IRAudio {

class AudioManager;

using MidiChannel = unsigned char;
using MidiStatus = unsigned char;
using CCData = unsigned char;
using CCMessage = unsigned char;

constexpr CCData kCCFalse = 0xFF;

enum MidiInInterfaces {
    MIDI_IN_UMC,
    MIDI_IN_FOCUSRITE,
    MIDI_IN_MPK,
    MIDI_IN_OP1,
    NUM_MIDI_IN_INTERFACES
};

enum MidiOutInterfaces {
    MIDI_OUT_UMC,
    MIDI_OUT_FOCUSRITE,
    MIDI_OUT_MPK,
    MIDI_OUT_OP1,
    NUM_MIDI_OUT_INTERFACES
};

enum MidiDeviceType { MIDI_DEVICE_TYPE_IN, MIDI_DEVICE_TYPE_OUT };

enum MidiChannels {
    kMidiChannel1 = 0,
    kMidiChannel2 = 1,
    kMidiChannel3 = 2,
    kMidiChannel4 = 3,
    kMidiChannel5 = 4,
    kMidiChannel6 = 5,
    kMidiChannel7 = 6,
    kMidiChannel8 = 7,
    kMidiChannel9 = 8,
    kMidiChannel10 = 9,
    kMidiChannel11 = 10,
    kMidiChannel12 = 11,
    kMidiChannel13 = 12,
    kMidiChannel14 = 13,
    kMidiChannel15 = 14,
    kMidiChannel16 = 15,
    kMidiChannelAll = 16

};

const char *const kMidiInInterfaceName_UMC = "UMC1820 MIDI In";
const char *const kMidiInInterfaceName_FOCUSRITE = "Focusrite USB MIDI";
const char *const kMidiInInterfaceName_MPK = "MPKmini2";
const char *const kMidiInInterfaceName_OP1 = "OP-1 Midi Device";

const char *const kMidiOutInterfaceName_UMC = "UMC1820 MIDI Out";
const char *const kMidiOutInterfaceName_FOCUSRITE = "Focusrite USB MIDI";
const char *const kMidiOutInterfaceName_MPK = "MPKmini2";
const char *const kMidiOutInterfaceName_OP1 = "OP-1 Midi Device";

// map midi in interface to name
const char *const kMidiInInterfaceNames[NUM_MIDI_IN_INTERFACES] = {
    kMidiInInterfaceName_UMC, kMidiInInterfaceName_FOCUSRITE, kMidiInInterfaceName_MPK,
    kMidiInInterfaceName_OP1};

// map midi out interface to name
const char *const kMidiOutInterfaceNames[NUM_MIDI_OUT_INTERFACES] = {
    kMidiOutInterfaceName_UMC, kMidiOutInterfaceName_FOCUSRITE, kMidiOutInterfaceName_MPK,
    kMidiOutInterfaceName_OP1};

const unsigned char kMidiMessageBits_STATUS = 0xF0;
const unsigned char kMidiMessageBits_CHANNEL = 0x0F;

constexpr MidiChannel kNumMidiChannels = 16;

constexpr MidiStatus kMidiStatus_NOTE_OFF = 0x80;
constexpr MidiStatus kMidiStatus_NOTE_ON = 0x90;
constexpr MidiStatus kMidiStatus_POLYPHONIC_KEY_PRESSURE = 0xA0;
constexpr MidiStatus kMidiStatus_CONTROL_CHANGE = 0xB0;
constexpr MidiStatus kMidiStatus_PROGRAM_CHANGE = 0xC0;
constexpr MidiStatus kMidiStatus_CHANNEL_PRESSURE = 0xD0;
constexpr MidiStatus kMidiStatus_PITCH_BEND = 0xE0;

enum IRMidiNote {
    NOTE_A0 = 21,
    NOTE_A0_SHARP = 22,
    NOTE_B0 = 23,
    NOTE_C1 = 24,
    NOTE_C1_SHARP = 25,
    NOTE_D1 = 26,
    NOTE_D1_SHARP = 27,
    NOTE_E1 = 28,
    NOTE_F1 = 29,
    NOTE_F1_SHARP = 30,
    NOTE_G1 = 31,
    NOTE_G1_SHARP = 32,
    NOTE_A1 = 33,
    NOTE_A1_SHARP = 34,
    NOTE_B1 = 35,
    NOTE_C2 = 36,
    NOTE_C2_SHARP = 37,
    NOTE_D2 = 38,
    NOTE_D2_SHARP = 39,
    NOTE_E2 = 40,
    NOTE_F2 = 41,
    NOTE_F2_SHARP = 42,
    NOTE_G2 = 43,
    NOTE_G2_SHARP = 44,
    NOTE_A2 = 45,
    NOTE_A2_SHARP = 46,
    NOTE_B2 = 47,
    NOTE_C3 = 48,
    NOTE_C3_SHARP = 49,
    NOTE_D3 = 50,
    NOTE_D3_SHARP = 51,
    NOTE_E3 = 52,
    NOTE_F3 = 53,
    NOTE_F3_SHARP = 54,
    NOTE_G3 = 55,
    NOTE_G3_SHARP = 56,
    NOTE_A3 = 57,
    NOTE_A3_SHARP = 58,
    NOTE_B3 = 59,
    NOTE_C4 = 60,
    NOTE_C4_SHARP = 61,
    NOTE_D4 = 62,
    NOTE_D4_SHARP = 63,
    NOTE_E4 = 64,
    NOTE_F4 = 65,
    NOTE_F4_SHARP = 66,
    NOTE_G4 = 67,
    NOTE_G4_SHARP = 68,
    NOTE_A4 = 69,
    NOTE_A4_SHARP = 70,
    NOTE_B4 = 71,
    NOTE_C5 = 72,
    NOTE_C5_SHARP = 73,
    NOTE_D5 = 74,
    NOTE_D5_SHARP = 75,
    NOTE_E5 = 76,
    NOTE_F5 = 77,
    NOTE_F5_SHARP = 78,
    NOTE_G5 = 79,
    NOTE_G5_SHARP = 80,
    NOTE_A5 = 81,
    NOTE_A5_SHARP = 82,
    NOTE_B5 = 83,
    NOTE_C6 = 84,
    NOTE_C6_SHARP = 85,
    NOTE_D6 = 86,
    NOTE_D6_SHARP = 87,
    NOTE_E6 = 88,
    NOTE_F6 = 89,
    NOTE_F6_SHARP = 90,
    NOTE_G6 = 91,
    NOTE_G6_SHARP = 92,
    NOTE_A6 = 93,
    NOTE_A6_SHARP = 94,
    NOTE_B6 = 95,
    NOTE_C7 = 96,
    NOTE_C7_SHARP = 97,
    NOTE_D7 = 98,
    NOTE_D7_SHARP = 99,
    NOTE_E7 = 100,
    NOTE_F7 = 101,
    NOTE_F7_SHARP = 102,
    NOTE_G7 = 103,
    NOTE_G7_SHARP = 104,
    NOTE_A7 = 105,
    NOTE_A7_SHARP = 106,
    NOTE_B7 = 107,
    NOTE_C8 = 108
};

constexpr int kMajorScaleSemitoneSteps[] = {2, 2, 1, 2, 2, 2, 1};
constexpr int kPentatonicScaleSteps[] = {2, 2, 3, 2, 3};
constexpr int kPentatonicDorianScaleSteps[] = {2, 1, 4, 2, 3};
constexpr int kPentatonicMinorScaleSteps[] = {2, 1, 4, 1, 4};

} // namespace IRAudio

#endif /* IR_AUDIO_TYPES_H */
