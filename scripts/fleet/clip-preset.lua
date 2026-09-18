-- clip-preset.lua — --config-preset used by attach-screenshots' clip step
-- (docs/agents/skills/attach-screenshots.md) for the --auto-record capture
-- pass. Keeps the composed clip small: a 640px-wide stream at 1.5 Mbps stays
-- well under fleet-clip's 1 MB MP4 warning budget for a several-second clip.
-- Height is left 0 so VideoManager derives it from each creation's render
-- aspect instead of pinning 16:9 on a creation that isn't.
config = {
    video_capture_fps = 30,
    video_capture_output_width = 640,
    video_capture_bitrate = 1500000,
}
