#ifndef IR_VOXEL_EDITOR_SESSIONS_H
#define IR_VOXEL_EDITOR_SESSIONS_H

#include "session_builder.hpp"

#include <string>

// Registered authoring sessions (#766 Part 2c). Each one is a recipe of editor
// gestures replayed against the live UI by the GUI-test harness; selected at
// run time with `--gui-session <name>`.
//
// `drag_probe` is the mechanism proof the plan sequences first (Phase 0's P0-3,
// deferred out of the probe slice until the aiming primitive existed): it walks
// the four gestures every entity recipe is built from — single-click place,
// left-drag box fill, the V erase-mode toggle, and a carve click — and asserts
// the live editable set's occupancy after each. The entity sessions (rock,
// mushroom, ant, bird, tree) land on this same spine.
namespace IRVoxelEditor::Session {

enum class Id {
    NONE,
    DRAG_PROBE,
};

// CLI name -> id. The accepted set is declared to IRArgs as an enum arg, so an
// unknown name is rejected at parse time with the allowed list; this maps the
// validated string to the typed id (the "enum, not string-match" rule at the
// CLI edge, .claude/rules/cpp-lua-enums.md).
inline Id idFromName(const std::string &name) {
    if (name == "drag_probe")
        return Id::DRAG_PROBE;
    return Id::NONE;
}

namespace detail {

// The four gestures, on the seeded ground plane at the scene's centre — clear
// of the left-column GUI panels in screen space and clear of the ground's edges
// so every anchor face is exposed.
inline Recipe buildDragProbe(IRMath::ivec3 sceneSize, IRMath::vec3 sceneOrigin) {
    Builder builder("drag_probe", sceneSize, sceneOrigin);

    // One step toward the camera from the seeded ground plane: the cells a
    // click on the ground's camera-facing face lands in.
    const int placeZ = sceneSize.z - 2;
    const int centerX = sceneSize.x / 2;
    const int centerY = sceneSize.y / 2;
    const IRMath::ivec3 placed(centerX, centerY, placeZ);
    const IRMath::ivec3 dragStart(centerX + 2, centerY, placeZ);
    const IRMath::ivec3 dragEnd(centerX + 5, centerY, placeZ);
    // Never touched by the recipe — catches an occupancy check that would pass
    // for a scene that is simply full (a seeded slab misread as authored work).
    const IRMath::ivec3 untouched(centerX - 3, centerY + 3, placeZ);

    builder.segment("place");
    builder.click(placed);
    builder.expectOccupancy(placed, true, "click_places_voxel");
    builder.expectOccupancy(untouched, false, "untouched_stays_empty");

    // P0-3: press, move, release across four cells commits one box fill.
    builder.segment("drag");
    builder.dragBox(dragStart, dragEnd);
    for (int x = dragStart.x; x <= dragEnd.x; ++x) {
        const IRMath::ivec3 cell(x, dragStart.y, placeZ);
        builder.expectOccupancy(cell, true, "drag_fills_" + std::to_string(x));
    }
    builder.expectOccupancy(untouched, false, "drag_leaves_untouched_empty");

    // V flips the left-click gestures to erase; the carve click then removes the
    // voxel the first segment placed, leaving the dragged run intact. The arm
    // segment parks the cursor first so a carve that misses is diagnosable: the
    // pick assertion separates "aimed at the wrong voxel" from "the gesture
    // never reached the erase path".
    builder.segment("erase_arm");
    builder.toggleEraseMode();
    builder.hover(placed);
    builder.expectPick(placed, "erase_aim_hits_target");
    builder.expectOccupancy(placed, true, "hover_does_not_edit");

    builder.segment("erase");
    builder.click(placed);
    builder.expectOccupancy(placed, false, "erase_removes_voxel");
    builder.expectOccupancy(dragStart, true, "erase_spares_drag_run");

    // Ctrl+S through the recipe's own chord scheduling. Phase 0's P0-1 proved
    // the editor's save dispatch with a hand-written event list; this proves the
    // builder's `chordKey` timing drives it too — the modifier has to still be
    // held when the key press drains. The saved file itself is checked by the
    // 2d runner, which owns resolving the editor's run directory.
    builder.segment("save");
    builder.save();
    builder.expectOccupancy(dragStart, true, "save_leaves_scene_intact");

    return builder.finish();
}

} // namespace detail

// Build the named session's recipe against the live scene dimensions. Returns
// an empty (not-ok) recipe for Id::NONE so callers can treat "no session" and
// "unbuildable session" the same way.
inline Recipe build(Id id, IRMath::ivec3 sceneSize, IRMath::vec3 sceneOrigin) {
    switch (id) {
    case Id::DRAG_PROBE:
        return detail::buildDragProbe(sceneSize, sceneOrigin);
    case Id::NONE:
        break;
    }
    return Recipe{};
}

} // namespace IRVoxelEditor::Session

#endif /* IR_VOXEL_EDITOR_SESSIONS_H */
