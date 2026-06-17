#ifndef AUDIO_PLAYBACK_H
#define AUDIO_PLAYBACK_H

#include <irreden/audio/ir_audio_types.hpp>

#include <irreden/math/ir_math_types.hpp>

#include <memory>
#include <string>

namespace IRAudio {

/// File-based audio playback: load and play `.wav` one-shots and streamed
/// music, mixed through per-category buses with master + per-bus volume,
/// looping, and fades. A thin wrapper over miniaudio's high-level engine
/// (`ma_engine` / `ma_sound` / `ma_sound_group`).
///
/// pImpl: miniaudio is an implementation detail and never leaks into this
/// header — only `audio_playback.cpp` defines `MINIAUDIO_IMPLEMENTATION`.
///
/// Threading: miniaudio runs its own audio thread internally. The public API
/// is main-thread only. Finished one-shot sounds are reclaimed on the main
/// thread by @ref tickPlayback (called once per frame from the input drain),
/// never from miniaudio's audio callback.
///
/// Forward seams (depth is #207/#208, not built here):
///  - listener + positional source — @ref setListenerPosition / @ref playSoundAt
///    set the one built-in `ma_engine` listener and a sound's world position;
///    #207 layers occlusion / biome ambience on top of this existing source.
///  - world-time scheduling / bus automation — keyed on `IRSim::tick()` /
///    `IRSim::cycleFraction`; #208 builds generative layering on that seam.
///    Not stubbed here — see `engine/audio/CLAUDE.md`.
class AudioPlayback {
  public:
    AudioPlayback();
    ~AudioPlayback();

    // Owns the audio device + a miniaudio engine — non-copyable, non-movable.
    AudioPlayback(const AudioPlayback &) = delete;
    AudioPlayback &operator=(const AudioPlayback &) = delete;
    AudioPlayback(AudioPlayback &&) = delete;
    AudioPlayback &operator=(AudioPlayback &&) = delete;

    /// True once the playback device + engine came up. When false (no device,
    /// or init failed) every `play*` call is a no-op returning
    /// @ref kInvalidSoundHandle and the setters do nothing — playback is
    /// simply absent, never a crash.
    bool isInitialized() const;

    /// Loads @p path fully into memory (decode-on-load) and plays it once
    /// through @p bus. Use for short SFX. @p loop keeps it repeating until
    /// @ref stop. Returns the handle, or @ref kInvalidSoundHandle on failure.
    SoundHandle
    playSound(const std::string &path, AudioBus bus, float volume = 1.0f, bool loop = false);
    /// Streams @p path from disk through the `Music` bus (no full decode).
    /// Defaults to looping — the typical background-music case.
    SoundHandle playMusic(const std::string &path, float volume = 1.0f, bool loop = true);

    /// Positional variant of @ref playSound: spatialized at world @p position
    /// against the engine listener (the #207 seam). v1 gives miniaudio's
    /// default pan/distance attenuation; occlusion/biome depth is #207.
    SoundHandle playSoundAt(
        const std::string &path,
        AudioBus bus,
        const IRMath::vec3 &position,
        float volume = 1.0f,
        bool loop = false
    );

    /// Stops and reclaims a sound now. No-op on an unknown / already-reaped handle.
    void stop(SoundHandle handle);
    /// Sets a live sound's linear volume (1.0 = unity). No-op on unknown handle.
    void setSoundVolume(SoundHandle handle, float volume);
    /// Fades a sound from silence to its current volume over @p milliseconds.
    void fadeIn(SoundHandle handle, unsigned int milliseconds);
    /// Fades a sound to silence over @p milliseconds, then stops it (reaped on
    /// the next @ref tickPlayback). No-op on unknown handle.
    void fadeOut(SoundHandle handle, unsigned int milliseconds);

    /// Sets a category bus's linear volume — scales every sound on the bus.
    void setBusVolume(AudioBus bus, float volume);
    /// Sets the master (engine) linear volume — scales the whole mix.
    void setMasterVolume(float volume);

    /// Moves the single engine listener (the #207 positional-audio seam).
    void setListenerPosition(const IRMath::vec3 &position);

    /// Reclaims finished non-looping sounds on the main thread. Called once per
    /// frame from the INPUT drain (next to `MidiIn::tick`); cheap when idle.
    void tickPlayback();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace IRAudio

#endif /* AUDIO_PLAYBACK_H */
