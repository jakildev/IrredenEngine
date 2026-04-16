#include <gtest/gtest.h>
#include <irreden/audio/music_theory.hpp>

// Tests for the constexpr helpers in music_theory.hpp.
// No audio hardware or managers required — all values are compile-time constants.

namespace {

using namespace IRAudio;

// ---------------------------------------------------------------------------
// rootMidiNote
// Formula: name + (octave + 1) * 12
// C4 = middle C = 60 is the industry-standard anchor.
// ---------------------------------------------------------------------------

TEST(MidiNoteTest, MiddleCIsNote60) {
    EXPECT_EQ(rootMidiNote(NOTE_NAME_C, 4), 60);
}

TEST(MidiNoteTest, A4Is69) {
    // A above middle C — concert pitch 440 Hz.
    EXPECT_EQ(rootMidiNote(NOTE_NAME_A, 4), 69);
}

TEST(MidiNoteTest, CMinus1IsNote0) {
    // Lowest standard MIDI note: C at octave -1.
    EXPECT_EQ(rootMidiNote(NOTE_NAME_C, -1), 0);
}

TEST(MidiNoteTest, OctaveSpanIs12) {
    // One octave up always adds exactly 12 semitones.
    EXPECT_EQ(rootMidiNote(NOTE_NAME_C, 5) - rootMidiNote(NOTE_NAME_C, 4), 12);
    EXPECT_EQ(rootMidiNote(NOTE_NAME_A, 5) - rootMidiNote(NOTE_NAME_A, 4), 12);
}

TEST(MidiNoteTest, IntOverloadMatchesEnumOverload) {
    // The int overload is used for computed pitch-class values.
    for (int pc = 0; pc < 12; ++pc) {
        EXPECT_EQ(rootMidiNote(pc, 4), rootMidiNote(static_cast<IRNoteName>(pc), 4));
    }
}

// ---------------------------------------------------------------------------
// Enharmonic equivalences
// Sharps and flats with the same semitone offset must be equal.
// ---------------------------------------------------------------------------

TEST(EnharmonicTest, CSharpEqualsDbFlat) {
    EXPECT_EQ(NOTE_NAME_C_SHARP, NOTE_NAME_D_FLAT);
    EXPECT_EQ(NOTE_NAME_C_SHARP, 1);
}

TEST(EnharmonicTest, DSharpEqualsEbFlat) {
    EXPECT_EQ(NOTE_NAME_D_SHARP, NOTE_NAME_E_FLAT);
    EXPECT_EQ(NOTE_NAME_D_SHARP, 3);
}

TEST(EnharmonicTest, FSharpEqualsGbFlat) {
    EXPECT_EQ(NOTE_NAME_F_SHARP, NOTE_NAME_G_FLAT);
    EXPECT_EQ(NOTE_NAME_F_SHARP, 6);
}

TEST(EnharmonicTest, ASharpEqualsBbFlat) {
    EXPECT_EQ(NOTE_NAME_A_SHARP, NOTE_NAME_B_FLAT);
    EXPECT_EQ(NOTE_NAME_A_SHARP, 10);
}

TEST(EnharmonicTest, TwelveUniquePitchClasses) {
    EXPECT_EQ(kNoteNamesCount, 12);
}

// ---------------------------------------------------------------------------
// Scale aliases
// SCALE_MAJOR and SCALE_MINOR must resolve to the same definition as their
// Church-mode counterparts.
// ---------------------------------------------------------------------------

TEST(ScaleAliasTest, MajorIsIonian) {
    EXPECT_EQ(getScaleSize(SCALE_MAJOR), getScaleSize(SCALE_IONIAN));
    const int* maj = getScaleIntervals(SCALE_MAJOR);
    const int* ion = getScaleIntervals(SCALE_IONIAN);
    for (int i = 0; i < getScaleSize(SCALE_MAJOR); ++i) {
        EXPECT_EQ(maj[i], ion[i]) << "interval mismatch at index " << i;
    }
}

TEST(ScaleAliasTest, MinorIsAeolian) {
    EXPECT_EQ(getScaleSize(SCALE_MINOR), getScaleSize(SCALE_AEOLIAN));
    const int* min = getScaleIntervals(SCALE_MINOR);
    const int* aeo = getScaleIntervals(SCALE_AEOLIAN);
    for (int i = 0; i < getScaleSize(SCALE_MINOR); ++i) {
        EXPECT_EQ(min[i], aeo[i]) << "interval mismatch at index " << i;
    }
}

// ---------------------------------------------------------------------------
// Scale sizes
// ---------------------------------------------------------------------------

TEST(ScaleSizeTest, DiatonicModesHaveSevenNotes) {
    EXPECT_EQ(getScaleSize(SCALE_IONIAN),     7);
    EXPECT_EQ(getScaleSize(SCALE_DORIAN),     7);
    EXPECT_EQ(getScaleSize(SCALE_PHRYGIAN),   7);
    EXPECT_EQ(getScaleSize(SCALE_LYDIAN),     7);
    EXPECT_EQ(getScaleSize(SCALE_MIXOLYDIAN), 7);
    EXPECT_EQ(getScaleSize(SCALE_AEOLIAN),    7);
    EXPECT_EQ(getScaleSize(SCALE_LOCRIAN),    7);
}

TEST(ScaleSizeTest, PentatonicHasFiveNotes) {
    EXPECT_EQ(getScaleSize(SCALE_PENTATONIC_MAJOR), 5);
    EXPECT_EQ(getScaleSize(SCALE_PENTATONIC_MINOR), 5);
}

TEST(ScaleSizeTest, WholeToneHasSixNotes) {
    EXPECT_EQ(getScaleSize(SCALE_WHOLE_TONE), 6);
}

TEST(ScaleSizeTest, ChromaticHasTwelveNotes) {
    EXPECT_EQ(getScaleSize(SCALE_CHROMATIC), 12);
}

TEST(ScaleSizeTest, DiminishedHasEightNotes) {
    EXPECT_EQ(getScaleSize(SCALE_DIMINISHED_WHOLE_HALF), 8);
    EXPECT_EQ(getScaleSize(SCALE_DIMINISHED_HALF_WHOLE), 8);
}

// ---------------------------------------------------------------------------
// Scale intervals — spot-check canonical values
// ---------------------------------------------------------------------------

TEST(ScaleIntervalsTest, MajorScaleIntervals) {
    // Ionian / Major: W W H W W W H = semitones {0,2,4,5,7,9,11}
    const int expected[] = {0, 2, 4, 5, 7, 9, 11};
    const int* intervals = getScaleIntervals(SCALE_MAJOR);
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(intervals[i], expected[i]) << "major interval mismatch at " << i;
    }
}

TEST(ScaleIntervalsTest, NaturalMinorScaleIntervals) {
    // Aeolian: W H W W H W W = {0,2,3,5,7,8,10}
    const int expected[] = {0, 2, 3, 5, 7, 8, 10};
    const int* intervals = getScaleIntervals(SCALE_MINOR);
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(intervals[i], expected[i]) << "minor interval mismatch at " << i;
    }
}

TEST(ScaleIntervalsTest, ChromaticScaleIsAllTwelveSemitones) {
    const int* intervals = getScaleIntervals(SCALE_CHROMATIC);
    for (int i = 0; i < 12; ++i) {
        EXPECT_EQ(intervals[i], i) << "chromatic interval mismatch at " << i;
    }
}

TEST(ScaleIntervalsTest, PentatonicMajorIntervals) {
    // {0, 2, 4, 7, 9}
    const int expected[] = {0, 2, 4, 7, 9};
    const int* intervals = getScaleIntervals(SCALE_PENTATONIC_MAJOR);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(intervals[i], expected[i]) << "pentatonic major mismatch at " << i;
    }
}

TEST(ScaleIntervalsTest, AllScalesStartOnZero) {
    for (int m = 0; m < static_cast<int>(NUM_SCALE_MODES); ++m) {
        auto mode = static_cast<IRScaleMode>(m);
        EXPECT_EQ(getScaleIntervals(mode)[0], 0) << "scale " << m << " does not start on 0";
    }
}

TEST(ScaleIntervalsTest, AllScalesHaveValidSize) {
    for (int m = 0; m < static_cast<int>(NUM_SCALE_MODES); ++m) {
        auto mode = static_cast<IRScaleMode>(m);
        int sz = getScaleSize(mode);
        EXPECT_GE(sz, 5)  << "scale " << m << " too small";
        EXPECT_LE(sz, 12) << "scale " << m << " too large";
    }
}

// ---------------------------------------------------------------------------
// kNumScaleModes sentinel
// ---------------------------------------------------------------------------

TEST(ScaleModeCountTest, SentinelMatchesArraySize) {
    EXPECT_EQ(kNumScaleModes, static_cast<std::size_t>(NUM_SCALE_MODES));
}

} // namespace
