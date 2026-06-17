#include <gtest/gtest.h>

#include <irreden/audio/audio_playback.hpp>

// Regression for the AudioPlayback graceful-degradation contract (#1813).
// On headless CI (no audio device, ma_engine_init fails) every play* call must
// return kInvalidSoundHandle and every setter must be a no-op — never a crash.
// On a dev machine the same no-crash invariant holds for bad file paths and
// kInvalidSoundHandle-valued handles.

namespace {

using namespace IRAudio;

class AudioPlaybackTest : public ::testing::Test {
  protected:
    AudioPlayback playback;
};

TEST_F(AudioPlaybackTest, IsInitializedDoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE(playback.isInitialized());
}

TEST_F(AudioPlaybackTest, PlaySoundBadPathReturnsInvalidHandle) {
    // Whether the device is present or absent, a nonexistent path must
    // produce kInvalidSoundHandle, never UB or a crash.
    const SoundHandle h =
        playback.playSound("/irreden_test_audio_nonexistent.wav", AudioBus::UI);
    EXPECT_EQ(h, kInvalidSoundHandle);
}

TEST_F(AudioPlaybackTest, PlayMusicBadPathReturnsInvalidHandle) {
    const SoundHandle h =
        playback.playMusic("/irreden_test_audio_nonexistent.ogg");
    EXPECT_EQ(h, kInvalidSoundHandle);
}

TEST_F(AudioPlaybackTest, PlaySoundAtBadPathReturnsInvalidHandle) {
    const SoundHandle h = playback.playSoundAt(
        "/irreden_test_audio_nonexistent.wav", AudioBus::CREATURE,
        IRMath::vec3{0.f, 0.f, 0.f});
    EXPECT_EQ(h, kInvalidSoundHandle);
}

TEST_F(AudioPlaybackTest, InvalidHandleOpsDoNotCrash) {
    EXPECT_NO_FATAL_FAILURE(playback.stop(kInvalidSoundHandle));
    EXPECT_NO_FATAL_FAILURE(playback.setSoundVolume(kInvalidSoundHandle, 0.5f));
    EXPECT_NO_FATAL_FAILURE(playback.fadeIn(kInvalidSoundHandle, 100u));
    EXPECT_NO_FATAL_FAILURE(playback.fadeOut(kInvalidSoundHandle, 100u));
}

TEST_F(AudioPlaybackTest, GlobalSettersDoNotCrash) {
    // No-ops when uninitialized; well-behaved when initialized.
    EXPECT_NO_FATAL_FAILURE(playback.setBusVolume(AudioBus::MUSIC, 0.5f));
    EXPECT_NO_FATAL_FAILURE(playback.setBusVolume(AudioBus::UI, 0.0f));
    EXPECT_NO_FATAL_FAILURE(playback.setMasterVolume(0.75f));
    EXPECT_NO_FATAL_FAILURE(
        playback.setListenerPosition(IRMath::vec3{0.f, 0.f, 0.f}));
}

TEST_F(AudioPlaybackTest, TickPlaybackDoesNotCrash) {
    for (int i = 0; i < 10; ++i) {
        EXPECT_NO_FATAL_FAILURE(playback.tickPlayback());
    }
}

} // namespace
