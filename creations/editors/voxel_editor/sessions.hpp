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
//
// `rock` is the first committed entity (#766 Part 2d): an irregular, no-symmetry,
// single-layer blob. It clears the seeded ground slab down to a small central
// footprint (four erase-mode box drags on the flat plane, before any rock voxel
// exists to occlude the corner aims), then builds three asymmetric layers on that
// footprint and carves two base corners, so the saved `.vxs` is the rock alone.
namespace IRVoxelEditor::Session {

enum class Id {
    NONE,
    DRAG_PROBE,
    ROCK,
};

// CLI name -> id. The accepted set is declared to IRArgs as an enum arg, so an
// unknown name is rejected at parse time with the allowed list; this maps the
// validated string to the typed id (the "enum, not string-match" rule at the
// CLI edge, .claude/rules/cpp-lua-enums.md).
inline Id idFromName(const std::string &name) {
    if (name == "drag_probe")
        return Id::DRAG_PROBE;
    if (name == "rock")
        return Id::ROCK;
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

// The rock — an irregular, no-symmetry, single-layer blob (#766 Part 2d). The
// recipe assumes a scene at least ~11 wide in x/y and ~12 tall (the default 16³
// satisfies it); the footprint and layers derive from the live dims so it stays
// centred and clear of the left-column GUI panels at any conforming size.
//
// Sequence: (1) clear the seeded ground slab down to a 5×5 central footprint,
// (2) build three asymmetric layers on that footprint, (3) carve two base
// corners for irregularity, (4) save. The clear runs first, while the ground is
// a single flat layer and every corner face is exposed — once rock voxels sit
// above the plane they occlude the (1,1,1) march to the cells behind them.
inline Recipe buildRock(IRMath::ivec3 sceneSize, IRMath::vec3 sceneOrigin) {
    Builder builder("rock", sceneSize, sceneOrigin);

    const int gz = sceneSize.z - 1; // seeded ground plane (local z)
    const int cx = sceneSize.x / 2;
    const int cy = sceneSize.y / 2;
    // 5×5 footprint kept as the rock's flat base; the rest of the ground plane
    // is erased so the saved asset is the rock, not a rock on a full slab.
    const IRMath::ivec3 fpLo(cx - 2, cy - 2, gz);
    const IRMath::ivec3 fpHi(cx + 2, cy + 2, gz);

    // --- Clear the ground slab down to the footprint --------------------
    // Four erase boxes framing the footprint. A box-drag erases the grid AABB
    // between its two clicked corners, so a single drag clears a whole strip;
    // the corners are all on the flat plane (no rock yet), so each is aimable.
    builder.segment("clear_ground");
    builder.toggleEraseMode();
    builder.dragBox(
        IRMath::ivec3(0, 0, gz),
        IRMath::ivec3(sceneSize.x - 1, fpLo.y - 1, gz)
    ); // low-y strip
    builder.dragBox(
        IRMath::ivec3(0, fpHi.y + 1, gz),
        IRMath::ivec3(sceneSize.x - 1, sceneSize.y - 1, gz)
    ); // high-y strip
    builder.dragBox(
        IRMath::ivec3(0, fpLo.y, gz),
        IRMath::ivec3(fpLo.x - 1, fpHi.y, gz)
    ); // low-x strip
    builder.dragBox(
        IRMath::ivec3(fpHi.x + 1, fpLo.y, gz),
        IRMath::ivec3(sceneSize.x - 1, fpHi.y, gz)
    );                         // high-x strip
    builder.toggleEraseMode(); // back to place mode for the build
    // Positive-fire: the footprint survived and the frame is gone. The corner /
    // edge checks also map how much of the plane the left GUI panels leave
    // clickable — a swallowed erase click fails its cleared-check loudly.
    builder.expectOccupancy(IRMath::ivec3(cx, cy, gz), true, "footprint_center_kept");
    builder.expectOccupancy(fpLo, true, "footprint_corner_kept");
    builder.expectOccupancy(IRMath::ivec3(0, 0, gz), false, "ground_tl_corner_cleared");
    builder
        .expectOccupancy(IRMath::ivec3(sceneSize.x - 1, 0, gz), false, "ground_tr_corner_cleared");
    builder
        .expectOccupancy(IRMath::ivec3(0, sceneSize.y - 1, gz), false, "ground_bl_corner_cleared");
    builder.expectOccupancy(
        IRMath::ivec3(sceneSize.x - 1, sceneSize.y - 1, gz),
        false,
        "ground_br_corner_cleared"
    );
    builder.expectOccupancy(IRMath::ivec3(cx, 0, gz), false, "ground_low_y_edge_cleared");
    builder.expectOccupancy(IRMath::ivec3(0, cy, gz), false, "ground_low_x_edge_cleared");

    // --- Build the blob on the footprint --------------------------------
    // Base: the full 5×5 on the layer one step toward the camera from the
    // footprint, each cell anchored to the footprint face below it.
    builder.segment("build_base");
    builder.dragBox(IRMath::ivec3(fpLo.x, fpLo.y, gz - 1), IRMath::ivec3(fpHi.x, fpHi.y, gz - 1));
    builder.expectOccupancy(IRMath::ivec3(cx, cy, gz - 1), true, "base_layer_filled");
    builder.expectOccupancy(fpLo, true, "footprint_survives_base");

    // Middle: an offset 3×4 slab — deliberately off-centre so the blob has no
    // mirror symmetry (the rock's defining property vs the mushroom/ant).
    builder.segment("build_mid");
    builder.dragBox(IRMath::ivec3(cx - 1, cy - 2, gz - 2), IRMath::ivec3(cx + 1, cy + 1, gz - 2));
    builder.expectOccupancy(IRMath::ivec3(cx, cy, gz - 2), true, "mid_layer_filled");

    // Cap: a 2×2 bump plus a single peak one cell off the axis.
    builder.segment("build_cap");
    builder.dragBox(IRMath::ivec3(cx - 1, cy - 1, gz - 3), IRMath::ivec3(cx, cy, gz - 3));
    builder.click(IRMath::ivec3(cx, cy - 1, gz - 4));
    builder.expectOccupancy(IRMath::ivec3(cx, cy - 1, gz - 4), true, "peak_placed");

    // --- Carve two base corners for irregularity ------------------------
    // Arm the carve in its own segment: hover the first corner and pick-assert
    // the aim while the voxel is still there (F-2c-4 — an erase is not
    // diagnosable from occupancy alone, and a pick check must fire before the
    // click removes its own target, or the ray falls through to the cell behind).
    const IRMath::ivec3 carveA(fpLo.x, fpHi.y, gz - 1);
    const IRMath::ivec3 carveB(fpHi.x, fpLo.y, gz - 1);
    builder.segment("carve_arm");
    builder.toggleEraseMode();
    builder.hover(carveA);
    builder.expectPick(carveA, "carve_aim_hits_corner");
    builder.expectOccupancy(carveA, true, "carve_target_present");

    builder.segment("carve");
    builder.click(carveA);
    builder.expectOccupancy(carveA, false, "carve_removed_corner_a");
    builder.click(carveB);
    builder.expectOccupancy(carveB, false, "carve_removed_corner_b");
    builder.expectOccupancy(IRMath::ivec3(cx, cy, gz - 1), true, "carve_spares_center");
    builder.toggleEraseMode();

    // --- Save -----------------------------------------------------------
    builder.segment("save");
    builder.save();
    builder.expectOccupancy(IRMath::ivec3(cx, cy, gz - 1), true, "scene_intact_after_save");

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
    case Id::ROCK:
        return detail::buildRock(sceneSize, sceneOrigin);
    case Id::NONE:
        break;
    }
    return Recipe{};
}

} // namespace IRVoxelEditor::Session

#endif /* IR_VOXEL_EDITOR_SESSIONS_H */
