#include <irreden/audio/audio_playback.hpp>

#include <irreden/ir_profile.hpp>

// miniaudio: this is the single translation unit that pulls in the
// implementation. Playback-only (no encoders); the high-level engine API
// brings the audio thread, decoding, mixing, sound groups (our buses),
// a listener + 3D spatialization (the #207 seam), and fades.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#include <miniaudio.h>

#include <array>
#include <cstddef>
#include <memory>
#include <unordered_map>

namespace IRAudio {

namespace {
/// True if @p bus is a real category (not the `COUNT` sentinel / out of range).
inline bool isValidBus(AudioBus bus) {
    const int index = static_cast<int>(bus);
    return index >= 0 && index < kNumAudioBuses;
}
} // namespace

struct AudioPlayback::Impl {
    ma_engine engine_{};
    bool initialized_ = false;
    // One mixer group per category bus, parented under the engine endpoint.
    std::array<ma_sound_group, kNumAudioBuses> buses_{};
    // Live sounds keyed by handle. Heap-owned so the `ma_sound` address is
    // stable while miniaudio's audio thread references it.
    std::unordered_map<SoundHandle, std::unique_ptr<ma_sound>> sounds_;
    // Monotonic; 0 is reserved for kInvalidSoundHandle.
    SoundHandle nextHandle_ = kInvalidSoundHandle + 1;

    ma_sound_group *busGroup(AudioBus bus) {
        return &buses_[static_cast<std::size_t>(static_cast<int>(bus))];
    }

    // Shared load+start path for every play* variant. `spatialPosition`
    // non-null enables spatialization at that world position (the #207 seam);
    // null plays the sound non-positionally (UI / music).
    SoundHandle start(
        const std::string &path,
        AudioBus bus,
        float volume,
        bool loop,
        bool stream,
        const IRMath::vec3 *spatialPosition
    ) {
        if (!initialized_) {
            return kInvalidSoundHandle;
        }
        if (!isValidBus(bus)) {
            IRE_LOG_WARN("AudioPlayback: invalid bus for '{}'", path);
            return kInvalidSoundHandle;
        }

        ma_uint32 flags = stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
        if (spatialPosition == nullptr) {
            flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
        }

        auto sound = std::make_unique<ma_sound>();
        const ma_result result = ma_sound_init_from_file(
            &engine_,
            path.c_str(),
            flags,
            busGroup(bus),
            nullptr,
            sound.get()
        );
        if (result != MA_SUCCESS) {
            IRE_LOG_WARN(
                "AudioPlayback: failed to load '{}' (miniaudio error {})",
                path,
                static_cast<int>(result)
            );
            return kInvalidSoundHandle;
        }

        ma_sound_set_volume(sound.get(), volume);
        ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);
        if (spatialPosition != nullptr) {
            ma_sound_set_position(
                sound.get(),
                spatialPosition->x,
                spatialPosition->y,
                spatialPosition->z
            );
        }
        ma_sound_start(sound.get());

        const SoundHandle handle = nextHandle_++;
        sounds_.emplace(handle, std::move(sound));
        return handle;
    }

    ma_sound *find(SoundHandle handle) {
        auto it = sounds_.find(handle);
        return it == sounds_.end() ? nullptr : it->second.get();
    }
};

AudioPlayback::AudioPlayback()
    : m_impl(std::make_unique<Impl>()) {
    const ma_result result = ma_engine_init(nullptr, &m_impl->engine_);
    if (result != MA_SUCCESS) {
        // No device / init failure is non-fatal: playback is simply absent.
        IRE_LOG_WARN(
            "AudioPlayback: ma_engine_init failed (error {}); file playback disabled",
            static_cast<int>(result)
        );
        return;
    }
    for (int i = 0; i < kNumAudioBuses; ++i) {
        if (ma_sound_group_init(
                &m_impl->engine_,
                0,
                nullptr,
                &m_impl->buses_[static_cast<std::size_t>(i)]
            ) != MA_SUCCESS) {
            IRE_LOG_WARN("AudioPlayback: failed to init bus group {}; file playback disabled", i);
            // Tear down groups already created + the engine, stay disabled.
            for (int j = 0; j < i; ++j) {
                ma_sound_group_uninit(&m_impl->buses_[static_cast<std::size_t>(j)]);
            }
            ma_engine_uninit(&m_impl->engine_);
            return;
        }
    }
    m_impl->initialized_ = true;
    IRE_LOG_INFO("AudioPlayback: file playback engine up ({} buses)", kNumAudioBuses);
}

AudioPlayback::~AudioPlayback() {
    if (!m_impl->initialized_) {
        return;
    }
    // Stop + free every live sound BEFORE the groups and engine die — mirrors
    // the MIDI stop-callbacks-before-teardown discipline.
    for (auto &entry : m_impl->sounds_) {
        ma_sound_uninit(entry.second.get());
    }
    m_impl->sounds_.clear();
    for (int i = 0; i < kNumAudioBuses; ++i) {
        ma_sound_group_uninit(&m_impl->buses_[static_cast<std::size_t>(i)]);
    }
    ma_engine_uninit(&m_impl->engine_);
}

bool AudioPlayback::isInitialized() const {
    return m_impl->initialized_;
}

SoundHandle
AudioPlayback::playSound(const std::string &path, AudioBus bus, float volume, bool loop) {
    return m_impl->start(path, bus, volume, loop, /*stream=*/false, /*spatialPosition=*/nullptr);
}

SoundHandle AudioPlayback::playMusic(const std::string &path, float volume, bool loop) {
    return m_impl
        ->start(path, AudioBus::MUSIC, volume, loop, /*stream=*/true, /*spatialPosition=*/nullptr);
}

SoundHandle AudioPlayback::playSoundAt(
    const std::string &path, AudioBus bus, const IRMath::vec3 &position, float volume, bool loop
) {
    return m_impl->start(path, bus, volume, loop, /*stream=*/false, &position);
}

void AudioPlayback::stop(SoundHandle handle) {
    auto it = m_impl->sounds_.find(handle);
    if (it == m_impl->sounds_.end()) {
        return;
    }
    ma_sound_uninit(it->second.get());
    m_impl->sounds_.erase(it);
}

void AudioPlayback::setSoundVolume(SoundHandle handle, float volume) {
    if (ma_sound *sound = m_impl->find(handle)) {
        ma_sound_set_volume(sound, volume);
    }
}

void AudioPlayback::fadeIn(SoundHandle handle, unsigned int milliseconds) {
    if (ma_sound *sound = m_impl->find(handle)) {
        // Ramp from silence to the sound's current volume.
        ma_sound_set_fade_in_milliseconds(sound, 0.0f, ma_sound_get_volume(sound), milliseconds);
    }
}

void AudioPlayback::fadeOut(SoundHandle handle, unsigned int milliseconds) {
    if (ma_sound *sound = m_impl->find(handle)) {
        // Fades to silence then stops; reaped on the next tickPlayback once it
        // is no longer playing.
        ma_sound_stop_with_fade_in_milliseconds(sound, milliseconds);
    }
}

void AudioPlayback::setBusVolume(AudioBus bus, float volume) {
    if (!m_impl->initialized_ || !isValidBus(bus)) {
        return;
    }
    ma_sound_group_set_volume(m_impl->busGroup(bus), volume);
}

void AudioPlayback::setMasterVolume(float volume) {
    if (m_impl->initialized_) {
        ma_engine_set_volume(&m_impl->engine_, volume);
    }
}

void AudioPlayback::setListenerPosition(const IRMath::vec3 &position) {
    if (m_impl->initialized_) {
        ma_engine_listener_set_position(&m_impl->engine_, 0, position.x, position.y, position.z);
    }
}

void AudioPlayback::tickPlayback() {
    if (!m_impl->initialized_ || m_impl->sounds_.empty()) {
        return;
    }
    // Reap finished one-shots (and faded-out sounds that have stopped). A
    // looping sound never auto-reaps — it lives until an explicit stop().
    for (auto it = m_impl->sounds_.begin(); it != m_impl->sounds_.end();) {
        ma_sound *sound = it->second.get();
        if (ma_sound_is_looping(sound) == MA_FALSE && ma_sound_is_playing(sound) == MA_FALSE) {
            ma_sound_uninit(sound);
            it = m_impl->sounds_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace IRAudio
