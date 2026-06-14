# Plan: Audio playback substrate: file-based SFX + music, bus mixer, Lua bindings

- **Issue:** #1813
- **Model:** opus
- **Date:** 2026-06-14

## Scope
Add a file-based audio playback substrate to `engine/audio/` (today MIDI-only).
One opus PR delivers: miniaudio vendored + cross-platform build wiring; load/play
`.wav` one-shots and streamed `.ogg` music; a category **bus mixer** (creature /
environment / ability / UI / music, per `irreden/docs/sound_design.md`) with
master + per-bus volume, looping, and fade-in/out; **Lua bindings**
(`IRAudio.playSound/playMusic`, bus + master volume) using the enum-as-Lua-table
convention; and two documented **forward seams** (listener/positional source;
bus/scheduler keyed on world-time) that #207 (spatial) and #208 (dynamic) build on
top of. Build the substrate jam-ready; do NOT build spatial/dynamic depth — those
are #207/#208, and ECS bind-points are #610.

## One task or a stack? — ONE opus PR (decision)
miniaudio's high-level `ma_engine` API collapses the runtime complexity: device
init, the audio thread, decoding (`.wav`/`.ogg` built-in), mixing, **sound groups
(= our buses)**, a **listener + 3D spatialization (= the #207 seam)**, per-sound
volume/loop, and **fades** are all built in. So the new engine code is a thin
wrapper + the `IRAudio::` API + a bus enum + Lua bindings + a demo — one cohesive
vertical slice, not a multi-PR build-out. The genuinely hard, separable work
(spatial depth #207, dynamic layering #208, ECS bind-points #610) is already
carved into its own tickets, so keeping #1813 a single task respects that split
and avoids merge-latency on interdependent same-file work. **If the implementing
worker finds it truly exceeds one PR**, the only sanctioned fault line is
[substrate + C++ playback API] → [bus mixer + Lua bindings + demo] filed as a
*stacked* follow-up (`**Blocked by:** #<this>`), never flat siblings on the same
new files (the #1370 conflicting-trio trap).

## Library decision (deliverable 1) — miniaudio, high-level `ma_engine`
Use miniaudio's high-level engine API (`ma_engine` / `ma_sound` / `ma_sound_group`),
NOT the low-level device/decoder API. Record in `engine/audio/CLAUDE.md`:
- single-header, permissive (public-domain / MIT-0), cross-platform
  (CoreAudio / WASAPI / ALSA) with built-in `.wav`/`.ogg`/`.flac`/`.mp3` decoding,
  mixing, sound groups, fades, and 3D spatialization;
- covers the jam need now AND the spatial (#207) / dynamic (#208) roadmap with no
  second migration;
- `ma_sound_group` is the per-category bus mixer for free; `ma_engine` owns one
  listener (the #207 seam); `ma_sound_set_fade_in_milliseconds` covers fades.

## Approach (ordered)
1. **Vendor miniaudio.** Single-header. Use FetchContent for consistency with
   RtMidi/RtAudio (`engine/audio/CMakeLists.txt:61-93`):
   `FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git GIT_TAG <pinned-tag> UPDATE_DISCONNECTED TRUE)`.
   (Alternative: drop `miniaudio.h` into `engine/audio/include/irreden/audio/vendor/`
   — fine for one header, but FetchContent matches the module pattern. Pin a tag,
   do NOT track master.)
2. **CMake wiring** (`engine/audio/CMakeLists.txt`): add `src/audio_playback.cpp`
   to the `IrredenEngineAudio` `add_library` list (`:1-7`);
   `target_include_directories(IrredenEngineAudio PRIVATE ${miniaudio_SOURCE_DIR})`;
   platform link — macOS: `-framework CoreAudio -framework AudioToolbox` under the
   existing Darwin guard used for RtAudio's CoreAudio pin; Linux: `pthread m dl`
   (miniaudio dlopen's ALSA at runtime, no `-lasound` link); Windows: nothing extra
   (WASAPI via system). Define `#define MINIAUDIO_IMPLEMENTATION` in exactly ONE TU
   (`src/audio_playback.cpp`) and slim with `MA_NO_ENCODING` (playback only;
   optionally `MA_NO_FLAC`/`MA_NO_MP3` if only wav/ogg needed).
3. **`AudioPlayback` class** (`include/irreden/audio/audio_playback.hpp` +
   `src/audio_playback.cpp`, namespace `IRAudio`). Owns one `ma_engine`, the five
   `ma_sound_group` buses (created under the engine at init), and a handle table of
   live `ma_sound`s. API:
   - `SoundHandle playSound(path, AudioBus bus, float volume, bool loop)` — one-shot
     or looped; load inline (`ma_sound_init_from_file`, `MA_SOUND_FLAG_DECODE` for
     short SFX) into the bus group, start.
   - `SoundHandle playMusic(path, float volume, bool loop)` — streamed
     (`MA_SOUND_FLAG_STREAM`) into the `Music` bus.
   - `stop(SoundHandle)`, `setSoundVolume(SoundHandle, float)`,
     `fadeOut(SoundHandle, ms)`, `fadeIn(SoundHandle, ms)`.
   - `setBusVolume(AudioBus, float)` (`ma_sound_group_set_volume`),
     `setMasterVolume(float)` (`ma_engine_set_volume`).
   - lifetime: a finished sound is *marked reclaimable* in miniaudio's end-callback
     and reaped on the main-thread tick — never freed from the audio callback.
4. **`AudioBus` enum** (`include/irreden/audio/ir_audio_types.hpp`, `IRAudio`):
   `enum class AudioBus : int { Creature, Environment, Ability, UI, Music, Count }`
   — closed categorical set per `sound_design.md`; drives the C++ group array AND
   the Lua table.
5. **Own it in `AudioManager`.** Add an `AudioPlayback` member
   (`include/irreden/audio/audio_manager.hpp` + `src/audio_manager.cpp`); init in
   the ctor after device discovery; tear down in the dtor (stop sounds +
   `ma_engine_uninit` BEFORE the engine object dies — mirrors the RtMidi
   stop-callbacks-before-teardown discipline). Optional per-frame `tickPlayback()`
   to reap finished handles, drained from the INPUT/UPDATE pipeline next to
   `MidiIn::tick()`. Expose through `IRAudio::` free functions in
   `include/irreden/ir_audio.hpp`, forwarding to `getAudioManager()`.
6. **Lua bindings.** New `engine/script/include/irreden/script/lua_audio_bindings.hpp`,
   inline `bindAudioApi(LuaScript&)` mirroring `lua_spatial_bindings.hpp` /
   `lua_sim_bindings.hpp`; register from `lua_script.cpp` (add a `bindLuaAudio()`
   call by `lua_script.cpp:644`). Expose:
   - `IRAudio.Bus` enum table via the `IR_BIND_*` macro pattern
     (`.claude/rules/cpp-lua-enums.md`; example `lua_command_bindings.hpp:42-48`) —
     NO string bus names at the boundary.
   - `IRAudio.playSound(path, busInt, volume?, loop?)`,
     `IRAudio.playMusic(path, volume?, loop?)`,
     `IRAudio.setBusVolume(busInt, vol)`, `IRAudio.setMasterVolume(vol)`,
     `IRAudio.stop(handle)`, `IRAudio.fadeOut(handle, ms)`. `play*` returns the
     integer `SoundHandle` so Lua can stop/fade it.
7. **Forward seams (architect, don't build depth):**
   - **Listener / positional source (#207 seam):** `IRAudio::setListenerPosition(vec3)`
     + `playSoundAt(path, bus, vec3)` setting `ma_sound` position (miniaudio does
     pan/attenuation). Document in `engine/audio/CLAUDE.md` that #207 layers
     occlusion / biome ambience on this existing listener+positional source — #207
     adds only the depth.
   - **Bus / scheduler-on-world-time (#208/#610 seam):** document (and lightly stub)
     a `scheduleOnTick`/bus-gain-automation hook keyed on `IRSim::tick()` /
     `IRSim::cycleFraction(name)` (`engine/prefabs/irreden/common/sim_clock.hpp:65,113`).
     v1 records the seam + where world-time is read; #208 builds generative layering
     on it. Do NOT implement a scheduler now.
8. **Demo + verification.** Add a minimal Lua-driven demo
   (`creations/demos/audio_playback/`) that loads+plays a `.wav` one-shot and an
   `.ogg` music loop and exercises per-bus + master volume and a fade — satisfying
   the acceptance criteria end-to-end from Lua. Commit a short, license-clean
   `.wav` + `.ogg` asset. Build+run on the author's host; the other platform lands
   via cross-host smoke (`fleet:needs-<host>-smoke`).

## Affected files
- `engine/audio/CMakeLists.txt` — FetchContent miniaudio, add `src/audio_playback.cpp`, platform link + include dir.
- `engine/audio/include/irreden/audio/audio_playback.hpp` (new) — `AudioPlayback` class.
- `engine/audio/src/audio_playback.cpp` (new) — `MINIAUDIO_IMPLEMENTATION` TU + impl.
- `engine/audio/include/irreden/audio/ir_audio_types.hpp` — add `AudioBus` enum + `SoundHandle` typedef.
- `engine/audio/include/irreden/audio/audio_manager.hpp` + `src/audio_manager.cpp` — own `AudioPlayback`, init/teardown, optional tick.
- `engine/audio/include/irreden/ir_audio.hpp` — `IRAudio::` playback + listener free-function surface.
- `engine/script/include/irreden/script/lua_audio_bindings.hpp` (new) — `bindAudioApi`.
- `engine/script/src/lua_script.cpp` — register `bindLuaAudio()` (`:644`).
- `engine/audio/CLAUDE.md` — library decision + rationale + the two documented seams; flip the "MIDI-only" scope note.
- `creations/demos/audio_playback/CMakeLists.txt` (new) — demo build target.
- `creations/CMakeLists.txt` (or equivalent demo-list file) — register the new demo so it builds with the rest of the creation suite.
- `test/audio/audio_playback_test.cpp` (optional) — headless load/handle/bus-volume unit test; gate device-dependent bits.

## Acceptance criteria
- A creation loads + plays a `.wav` one-shot and an `.ogg` music loop from Lua.
- Per-bus AND master volume audibly change output; one-shots and loops both work; a fade works.
- The miniaudio decision + spatial/dynamic rationale is recorded in `engine/audio/CLAUDE.md`.
- The listener/positional seam and the bus/scheduler-on-world-time seam exist and are documented as the #207/#208 build points.
- Builds + plays on macOS (CoreAudio) and Linux (the fleet env) — author host direct, other host via cross-host smoke.

## Gotchas
- **Threading:** miniaudio runs its own audio thread; `ma_sound` end-callbacks fire
  on it. Never touch ECS/Lua or free a handle from that callback — only
  mark-reclaimable and reap on the main-thread tick, exactly like the existing
  RtMidi/RtAudio callback discipline (`engine/audio/CLAUDE.md` threading note).
- **Teardown order:** stop all sounds and `ma_engine_uninit` BEFORE
  `AudioManager`/`g_audioManager` is destroyed (the callback-lifetime + device
  gotcha already documented for MIDI).
- **No ECS component in v1.** The API is handle-based (`IRAudio::` + Lua), not a
  `C_AudioSource` — ECS bind-points are #610. Keep ECS out of v1.
- **Decode vs stream:** short SFX → `MA_SOUND_FLAG_DECODE` (low-latency); music →
  `MA_SOUND_FLAG_STREAM` (don't load a whole track into RAM).
- **Pin the miniaudio tag** (don't FetchContent master); slim with `MA_NO_ENCODING`
  (+ `MA_NO_FLAC`/`MA_NO_MP3` if only wav/ogg) to cut compile time.
- **Headless CI:** opening a real device can fail on a headless box; guard the
  demo/test so a no-device environment degrades gracefully (log + skip) rather than
  asserting — mirror the MIDI "absent port" non-asserting goal (#1503).
- **Looped sound handle retention:** `playSound`/`playMusic` with `loop=true` return a `SoundHandle` the caller MUST retain to allow stop/fade; a lost handle cannot be stopped until `ma_engine_uninit` at teardown. Document in the API header and Lua binding comments.
- **Asset licensing:** the committed demo `.wav`/`.ogg` must be license-clean
  (CC0 / self-generated). A ~0.2 s blip + a few-second loop is enough.
