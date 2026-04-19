#ifndef IR_VIDEO_AUTO_SCREENSHOT_H
#define IR_VIDEO_AUTO_SCREENSHOT_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/system/ir_system_types.hpp>

namespace IRVideo {

/// One entry in an auto-screenshot shot list. The cycling system applies
/// @c zoom_ and @c cameraIso_ via @c IRRender before capture, waits the
/// configured settle frames, then triggers a composite screenshot.
///
/// @c label_ is printed to the log around each capture — it does not affect
/// the on-disk filename (@c IRVideo uses a running counter for that).
struct AutoScreenshotShot {
    float zoom_ = 1.0f;
    vec2 cameraIso_ = vec2(0.0f);
    const char *label_ = "shot";
};

/// Declarative config for @c createAutoScreenshotSystem. @c shots_ /
/// @c numShots_ point at a shot table owned by the caller — it must outlive
/// the game loop.
struct AutoScreenshotConfig {
    int warmupFrames_ = 10;
    int settleFrames_ = 3;
    const AutoScreenshotShot *shots_ = nullptr;
    int numShots_ = 0;
};

/// Parse @c --auto-screenshot<space>[frames] from argv. Returns @c true if
/// the flag is present. When present and followed by a positive integer,
/// @c *warmupFramesOut is set to that value; otherwise the default of 10 is
/// written. @c warmupFramesOut may be @c nullptr.
bool parseAutoScreenshotArgv(int argc, char **argv, int *warmupFramesOut);

/// Create a system that cycles through @c config.shots_ — one screenshot per
/// shot with @c settleFrames_ between shots — then calls
/// @c IRWindow::closeWindow(). The caller must append the returned @c SystemId
/// to the RENDER pipeline before @c registerPipeline fires.
///
/// Requires @c IREngine::init() has run (so the system manager is live).
IRSystem::SystemId createAutoScreenshotSystem(const AutoScreenshotConfig &config);

} // namespace IRVideo

#endif /* IR_VIDEO_AUTO_SCREENSHOT_H */
