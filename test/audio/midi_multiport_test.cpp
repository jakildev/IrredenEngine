#include <gtest/gtest.h>

#include <irreden/audio/midi_input_frame_buffer.hpp>
#include <irreden/audio/ir_audio_types.hpp>

// Routing tests for the per-frame inbound MIDI buffer that backs multi-port
// input (#1727). MidiInputFrameBuffer carries no RtMidi/hardware dependency,
// so the merged-vs-per-port routing — the acceptance-critical "two input ports
// open at once, each delivering with correct port identity" contract — is
// verified deterministically without a MIDI device.

namespace {

using namespace IRAudio;
using IRComponents::C_MidiMessage;

C_MidiMessage ccMessage(MidiChannel channel, unsigned char ccNumber, unsigned char ccValue) {
    return C_MidiMessage{buildMidiStatus(kMidiStatus_CONTROL_CHANGE, channel), ccNumber, ccValue};
}

C_MidiMessage noteOnMessage(MidiChannel channel, unsigned char note, unsigned char velocity) {
    return C_MidiMessage{buildMidiStatus(kMidiStatus_NOTE_ON, channel), note, velocity};
}

constexpr int kPortA = 1;
constexpr int kPortB = 3;
constexpr MidiChannel kChannel = 0;

TEST(MidiInputFrameBufferTest, PerPortCCIsolation) {
    MidiInputFrameBuffer buffer;
    buffer.insertCC(kPortA, kChannel, ccMessage(kChannel, 10, 64));
    buffer.insertCC(kPortB, kChannel, ccMessage(kChannel, 10, 20));

    // Same channel + CC number on two ports stays distinguishable per port.
    EXPECT_EQ(buffer.checkCC(kPortA, kChannel, 10), 64);
    EXPECT_EQ(buffer.checkCC(kPortB, kChannel, 10), 20);
}

TEST(MidiInputFrameBufferTest, UnopenedPortAndAbsentCcReturnFalse) {
    MidiInputFrameBuffer buffer;
    buffer.insertCC(kPortA, kChannel, ccMessage(kChannel, 10, 64));

    EXPECT_EQ(buffer.checkCC(99, kChannel, 10), kCCFalse);      // port never seen
    EXPECT_EQ(buffer.checkCC(kPortA, kChannel, 11), kCCFalse);  // cc never sent
    EXPECT_EQ(buffer.checkCC(kPortA, 5, 10), kCCFalse);         // other channel
}

TEST(MidiInputFrameBufferTest, MergedCcFoldsAllPorts) {
    MidiInputFrameBuffer buffer;
    buffer.insertCC(kPortA, kChannel, ccMessage(kChannel, 10, 64));
    buffer.insertCC(kPortB, kChannel, ccMessage(kChannel, 10, 20));

    // The merged view collapses same-channel+cc traffic; last write wins. The
    // per-port view (above) is how a consumer avoids that collision.
    EXPECT_EQ(buffer.checkCC(kChannel, 10), 20);
}

TEST(MidiInputFrameBufferTest, PerPortNoteIsolationAndMergedFold) {
    MidiInputFrameBuffer buffer;
    buffer.insertNoteOn(kPortA, kChannel, noteOnMessage(kChannel, 60, 100));
    buffer.insertNoteOn(kPortB, kChannel, noteOnMessage(kChannel, 64, 80));

    const auto &portANotes = buffer.notesOn(kPortA, kChannel);
    ASSERT_EQ(portANotes.size(), 1u);
    EXPECT_EQ(portANotes[0].getMidiNoteNumber(), 60);

    const auto &portBNotes = buffer.notesOn(kPortB, kChannel);
    ASSERT_EQ(portBNotes.size(), 1u);
    EXPECT_EQ(portBNotes[0].getMidiNoteNumber(), 64);

    // Merged view sees both ports' notes on the same channel.
    EXPECT_EQ(buffer.notesOn(kChannel).size(), 2u);
}

TEST(MidiInputFrameBufferTest, NoteOnAndNoteOffAreSeparate) {
    MidiInputFrameBuffer buffer;
    buffer.insertNoteOn(kPortA, kChannel, noteOnMessage(kChannel, 60, 100));
    buffer.insertNoteOff(kPortA, kChannel, noteOnMessage(kChannel, 60, 0));

    EXPECT_EQ(buffer.notesOn(kPortA, kChannel).size(), 1u);
    EXPECT_EQ(buffer.notesOff(kPortA, kChannel).size(), 1u);
}

TEST(MidiInputFrameBufferTest, PortlessInsertStaysMergedOnly) {
    MidiInputFrameBuffer buffer;
    buffer.insertCC(kChannel, ccMessage(kChannel, 7, 99)); // legacy port-less insert

    EXPECT_EQ(buffer.checkCC(kChannel, 7), 99);            // merged sees it
    EXPECT_EQ(buffer.checkCC(kPortA, kChannel, 7), kCCFalse); // no port view written
}

TEST(MidiInputFrameBufferTest, ClearWipesMergedAndPerPort) {
    MidiInputFrameBuffer buffer;
    buffer.insertCC(kPortA, kChannel, ccMessage(kChannel, 10, 64));
    buffer.insertNoteOn(kPortA, kChannel, noteOnMessage(kChannel, 60, 100));

    buffer.clear();

    EXPECT_EQ(buffer.checkCC(kChannel, 10), kCCFalse);
    EXPECT_EQ(buffer.checkCC(kPortA, kChannel, 10), kCCFalse);
    EXPECT_TRUE(buffer.notesOn(kChannel).empty());
    EXPECT_TRUE(buffer.notesOn(kPortA, kChannel).empty());
}

} // namespace
