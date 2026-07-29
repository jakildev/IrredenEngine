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
//
// `mushroom` is the second committed entity (#766 Part 2 mushroom slice): a
// radially-symmetric cap + stem authored with X+Y mirror symmetry, so one
// authored quadrant fills all four. It is the positive-fire regression test for
// the F-1.2 mirror fix (this PR wires applyMirrors into the editor's edit path):
// every stem/cap assertion checks a *mirror-created* cell, and a hover probe
// aims at a voxel the recipe never clicked — all of which read empty / wrong if
// mirroring is broken. Two layers (stem default, cap added via K), a hide/show
// visibility pair, and a save→reload round-trip round out the acceptance.
namespace IRVoxelEditor::Session {

enum class Id {
    NONE,
    DRAG_PROBE,
    ROCK,
    MUSHROOM,
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
    if (name == "mushroom")
        return Id::MUSHROOM;
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
// recipe assumes a scene at least ~11 wide in x/y and 5 deep in z (the peak
// voxel sits at gz - 4; the default 16³ satisfies both); the footprint and
// layers derive from the live dims so it stays centred and clear of the
// left-column GUI panels at any conforming size.
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
    // Arm each carve in its own segment: hover the corner and pick-assert
    // the aim while the voxel is still there (F-2c-4 — an erase is not
    // diagnosable from occupancy alone, and a pick check must fire before the
    // click removes its own target, or the ray falls through to the cell behind).
    const IRMath::ivec3 carveA(fpLo.x, fpHi.y, gz - 1);
    const IRMath::ivec3 carveB(fpHi.x, fpLo.y, gz - 1);
    builder.segment("carve_arm_a");
    builder.toggleEraseMode();
    builder.hover(carveA);
    builder.expectPick(carveA, "carve_aim_hits_corner_a");
    builder.expectOccupancy(carveA, true, "carve_target_present_a");

    builder.segment("carve_a");
    builder.click(carveA);
    builder.expectOccupancy(carveA, false, "carve_removed_corner_a");

    // carveB is armed in its own segment too — a hover/pick-assert issued
    // within the same shot as its click is evaluated after all of that
    // shot's inputs fire (segments are shot boundaries, not per-event
    // checkpoints), so it would see the post-click state instead of pre-arm.
    builder.segment("carve_arm_b");
    builder.hover(carveB);
    builder.expectPick(carveB, "carve_aim_hits_corner_b");
    builder.expectOccupancy(carveB, true, "carve_target_present_b");

    builder.segment("carve_b");
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

// The mushroom — a radially-symmetric cap + stem authored with X+Y mirror
// symmetry (#766 F-1.6 PR-2). The mirror planes sit at the scene centre (offset
// (size-1)/2 per axis, matching the editor's applyEdit reflection), so a voxel
// authored at the low/front quadrant cell (cx-1, cy-1) fills all four of
// {cx-1,cx}×{cy-1,cy}. Assumes a scene at least ~12 wide in x/y and ~6 deep in z
// (the default 16³ satisfies both); geometry derives from the live dims so it
// stays centred and clear of the left-column GUI panels.
//
// Sequence: (1) clear the seeded ground slab down to the 2×2 stem base with
// symmetry OFF (framing erase drags on the flat plane, every corner exposed),
// (2) enable X+Y mirror and grow a 2×2 stem column, asserting the mirror-created
// cells and hover-probing one (the F-1.2 positive fire), (3) add a cap layer (K)
// and grow a wider disc, (4) hide/show the cap layer, (5) save + reload.
inline Recipe buildMushroom(IRMath::ivec3 sceneSize, IRMath::vec3 sceneOrigin) {
    Builder builder("mushroom", sceneSize, sceneOrigin);

    const int gz = sceneSize.z - 1; // seeded ground plane (local z)
    const int cx = sceneSize.x / 2; // mirror plane between cx-1 and cx
    const int cy = sceneSize.y / 2;
    const int lx = cx - 1; // low/front quadrant cell adjacent to the X plane
    const int ly = cy - 1; // ... and the Y plane

    // --- Clear the ground slab down to the 2×2 stem base (symmetry OFF) ------
    // Four erase strips framing {cx-1,cx}×{cy-1,cy}; all corners are on the flat
    // plane (no stem yet) so each is aimable, same as the rock's ground clear.
    builder.segment("clear_ground");
    builder.toggleEraseMode();
    builder.dragBox(
        IRMath::ivec3(0, 0, gz),
        IRMath::ivec3(sceneSize.x - 1, ly - 1, gz)
    ); // low-y strip
    builder.dragBox(
        IRMath::ivec3(0, cy + 1, gz),
        IRMath::ivec3(sceneSize.x - 1, sceneSize.y - 1, gz)
    );                                                                        // high-y strip
    builder.dragBox(IRMath::ivec3(0, ly, gz), IRMath::ivec3(lx - 1, cy, gz)); // low-x strip
    builder.dragBox(
        IRMath::ivec3(cx + 1, ly, gz),
        IRMath::ivec3(sceneSize.x - 1, cy, gz)
    );                         // high-x strip
    builder.toggleEraseMode(); // back to place mode
    builder.expectOccupancy(IRMath::ivec3(lx, ly, gz), true, "stem_base_kept");
    builder.expectOccupancy(IRMath::ivec3(cx, cy, gz), true, "stem_base_far_corner_kept");
    builder.expectOccupancy(IRMath::ivec3(0, 0, gz), false, "ground_corner_cleared");
    builder.expectOccupancy(
        IRMath::ivec3(sceneSize.x - 1, sceneSize.y - 1, gz),
        false,
        "ground_far_cleared"
    );

    // --- Stem: a 2×2 central column, authored one quadrant with X+Y mirror ---
    builder.segment("stem");
    builder.enableSymmetry(true, true, false);
    builder.click(IRMath::ivec3(lx, ly, gz - 1));
    builder.click(IRMath::ivec3(lx, ly, gz - 2));
    builder.click(IRMath::ivec3(lx, ly, gz - 3));
    // Positive fire for the mirror fix: every one of these is a *mirror-created*
    // cell — the recipe only ever clicked (lx, ly, ·), so a broken mirror (the
    // F-1.2 regression this PR fixes) leaves them empty and fails the run. The
    // checks read the live editable set, not the shadow model, so they can't be
    // fooled by the builder's own bookkeeping.
    builder.expectOccupancy(IRMath::ivec3(cx, ly, gz - 1), true, "stem_x_mirror_filled");
    builder.expectOccupancy(IRMath::ivec3(lx, cy, gz - 1), true, "stem_y_mirror_filled");
    builder.expectOccupancy(IRMath::ivec3(cx, cy, gz - 1), true, "stem_xy_mirror_filled");
    builder.expectOccupancy(IRMath::ivec3(cx, cy, gz - 3), true, "stem_xy_mirror_top_filled");

    // --- Cap: a wide flat disc on a new layer -------------------------------
    // Grown with -x-face and -z-face clicks only. A single voxel's -y face does
    // not reliably place its -y neighbour at the cardinal (yaw-0) camera — the
    // iso-projected -y-face aim mis-picks and the click no-ops — so the recipe
    // never depends on it and takes the cap's y-thickness from the Y mirror
    // instead (see docs/design/editor-authoring-friction.md). The disc is
    // therefore wide in x with a 2-cell y depth: a flat cap, symmetric about
    // both mirror planes.
    const int cz = gz - 4; // main cap disc, one step above the stem top
    builder.addLayer();
    builder.segment("cap");
    builder.click(IRMath::ivec3(lx, ly, cz));     // over the stem top (-z anchor)
    builder.click(IRMath::ivec3(lx - 1, ly, cz)); // -x
    builder.click(IRMath::ivec3(lx - 2, ly, cz)); // wider -x
    // A second, narrower tier one step up (-z on the disc) for a domed profile.
    builder.click(IRMath::ivec3(lx, ly, cz - 1));
    builder.click(IRMath::ivec3(lx - 1, ly, cz - 1));
    // Every check is a mirror-created cell — the recipe only ever clicked
    // (·, ly, ·) on the low-x side, so a broken mirror leaves these empty. The X
    // mirror supplies the far-x arm, the Y mirror the cap's y depth.
    builder.expectOccupancy(IRMath::ivec3(cx, cy, cz), true, "cap_xy_mirror_centre"); // (8,8,cz)
    builder.expectOccupancy(
        IRMath::ivec3(cx + 2, ly, cz),
        true,
        "cap_x_mirror_arm"
    ); // (10,7,cz) mirror of (5,7)
    builder.expectOccupancy(
        IRMath::ivec3(lx - 1, cy, cz),
        true,
        "cap_y_mirror_arm"
    ); // (6,8,cz) Y mirror of (6,7)
    builder.expectOccupancy(
        IRMath::ivec3(cx + 1, cy, cz),
        true,
        "cap_xy_mirror_arm"
    ); // (9,8,cz) XY mirror of (6,7)
    builder.expectOccupancy(
        IRMath::ivec3(cx, cy, cz - 1),
        true,
        "cap_dome_mirror"
    ); // (8,8,cz-1) XY mirror of the dome tier

    // --- Visibility: hide then show the cap layer (positive fire) -----------
    builder.segment("cap_hide");
    builder.toggleActiveLayerVisibility();
    builder.expectOccupancy(IRMath::ivec3(lx, ly, cz), false, "cap_hidden_reads_empty");
    builder.expectOccupancy(IRMath::ivec3(lx, ly, gz - 1), true, "stem_visible_when_cap_hidden");
    builder.segment("cap_show");
    builder.toggleActiveLayerVisibility();
    builder.expectOccupancy(IRMath::ivec3(lx, ly, cz), true, "cap_shown_again");

    // --- Save + reload round-trip -------------------------------------------
    builder.segment("save");
    builder.save();
    builder.expectOccupancy(IRMath::ivec3(cx, cy, gz - 1), true, "scene_intact_after_save");
    builder.segment("reload");
    builder.reload();
    builder.expectOccupancy(IRMath::ivec3(lx, ly, gz - 1), true, "stem_survives_reload");
    builder.expectOccupancy(IRMath::ivec3(cx, cy, gz - 1), true, "stem_mirror_survives_reload");
    builder.expectOccupancy(IRMath::ivec3(lx, ly, cz), true, "cap_survives_reload");

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
    case Id::MUSHROOM:
        return detail::buildMushroom(sceneSize, sceneOrigin);
    case Id::NONE:
        break;
    }
    return Recipe{};
}

} // namespace IRVoxelEditor::Session

#endif /* IR_VOXEL_EDITOR_SESSIONS_H */
