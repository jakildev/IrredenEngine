#ifndef MUSIC_NOTES_H
#define MUSIC_NOTES_H

namespace IRAudio {
constexpr int kNumMusicNotes = 12;

constexpr int kMusicNoteOffestC = 0;
constexpr int kMusicNoteOffestCSharp = 1;
constexpr int kMusicNoteOffsetDFlat = 1;
constexpr int kMusicNoteOffestD = 2;
constexpr int kMusicNoteOffestDSharp = 3;
constexpr int kMusicNoteOffestEFlat = 3;
constexpr int kMusicNoteOffestE = 4;
constexpr int kMusicNoteOffestF = 5;
constexpr int kMusicNoteOffestFSharp = 6;
constexpr int kMusicNoteOffestGFlat = 6;
constexpr int kMusicNoteOffestG = 7;
constexpr int kMusicNoteOffestGSharp = 8;
constexpr int kMusicNoteOffestAFlat = 8;
constexpr int kMusicNoteOffestA = 9;
constexpr int kMusicNoteOffestASharp = 10;
constexpr int kMusicNoteOffestBFlat = 10;
constexpr int kMusicNoteOffestB = 11;

constexpr bool isWhiteKey(int note) {
    const int noteType = note % kNumMusicNotes;
    return (noteType == kMusicNoteOffestC || noteType == kMusicNoteOffestD ||
            noteType == kMusicNoteOffestE || noteType == kMusicNoteOffestF ||
            noteType == kMusicNoteOffestG || noteType == kMusicNoteOffestA ||
            noteType == kMusicNoteOffestB);
}

constexpr bool isBlackKey(int note) {
    return !isWhiteKey(note);
}

} // namespace IRAudio

#endif /* MUSIC_NOTES_H */
