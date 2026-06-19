-- audio_playback demo (engine #1813) — drives the IRAudio file-playback
-- substrate end-to-end from Lua. File paths resolve from the exe runtime dir
-- (the engine chdir's there at init), so the demo's data/audio/*.wav assets
-- live at "data/audio/...".
--
-- If no playback device comes up (e.g. a headless CI box), every IRAudio call
-- is a silent no-op and the play* calls return 0 — the script still runs
-- cleanly, it just makes no sound.

-- Master + per-bus mix levels.
IRAudio.setMasterVolume(0.8)
IRAudio.setBusVolume(IRAudio.Bus.MUSIC, 0.6)
IRAudio.setBusVolume(IRAudio.Bus.UI, 0.9)

-- Streamed, looping background music on the Music bus, swelling in over 1s.
local music = IRAudio.playMusic("data/audio/loop.wav", 0.7, true)
IRAudio.fadeIn(music, 1000)

-- A one-shot UI blip, decoded into memory and played once on the UI bus.
local blip = IRAudio.playSound("data/audio/blip.wav", IRAudio.Bus.UI, 1.0, false)

print(string.format("audio_demo: music handle=%d, blip handle=%d", music, blip))

-- fadeOut(music, 1500) would ramp the music to silence and stop it; left out
-- here so the loop keeps playing for the demo. stop(handle) stops immediately.
