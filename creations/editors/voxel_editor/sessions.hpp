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
//
// `ant` is the third committed entity (#766 Part 2f) and the largest session:
// an X-mirrored body on four named layers with six legs as three mirrored
// pairs, authored at `--scene-size 20 20 20`. It is the first recipe to enable
// the mirror *before* the ground clear, so the silhouette is carved from half
// the erase drags, and the first to walk the layer selection (`[` / `]`).
namespace IRVoxelEditor::Session {

enum class Id {
    NONE,
    DRAG_PROBE,
    ROCK,
    MUSHROOM,
    ANT,
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
    if (name == "ant")
        return Id::ANT;
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

// Body rows the ant occupies along y, back to front: abdomen 5, petiole 1,
// thorax 5, neck 1, head 2, antennae 2. The block is centred in the scene's y
// span so the ant stays clear of both ends of the plane.
inline constexpr int kAntBodyRows = 16;

// How far a leg tip lands from the body column it grows out of, and how many
// tiers the body stands above the ground plane. Both bound the scene the ant
// fits in, so the precondition below derives its minimums from them rather
// than restating them as literals that could drift apart.
inline constexpr int kAntLegReach = 5;
inline constexpr int kAntBodyTiers = 2;

// The ant — the plan's PR-3 and the largest session (#766 F-1.6). A bilaterally
// symmetric body authored entirely from its low-x half under an **X mirror**,
// with six legs as three mirrored pairs and four named layers (abdomen, thorax,
// legs, head) stacked on the default layer the kept ground footprint lands on.
// Runs at the plan's `--scene-size 20 20 20`; anything that would clip the ant
// is a recipe error rather than a silently smaller animal.
//
// Sequence: (1) enable the X mirror **before** the ground clear, so the eight
// erase drags that carve the ant's silhouette out of the seeded slab are
// themselves authored on one half only, (2) abdomen layer — two domed tiers,
// (3) thorax layer, (4) legs layer — three chains of `-x`-face clicks marching
// out of the thorax side, each mirrored into its opposite leg, (5) head layer
// plus eyes, (6) `[` back to the legs layer and hide/show it, (7) save + reload.
//
// Every occupancy check names a **mirror-created** cell or a cell the mirror was
// responsible for clearing, so the whole session is a positive fire for the
// F-1.2 fix — the recipe never clicks past the mirror plane.
//
// The body grows only in `-x` and `-z` from the seeded plane: a voxel's `-y`
// face does not reliably place its `-y` neighbour at the cardinal camera
// (#2575), so no gesture here depends on one. Rows along y are reached from the
// ground plane below them instead, which is why each tier is a box drag whose
// two corners sit over kept footprint rather than a march along the body.
inline Recipe buildAnt(IRMath::ivec3 sceneSize, IRMath::vec3 sceneOrigin) {
    using IRMath::ivec3;
    Builder builder("ant", sceneSize, sceneOrigin);

    const int gz = sceneSize.z - 1;     // seeded ground plane (local z)
    const int lx = sceneSize.x / 2 - 1; // low-x cell adjacent to the mirror plane
    const int y0 = (sceneSize.y - kAntBodyRows) / 2;
    // The X mirror pairs cell x with sceneSize.x-1-x (plane at (size-1)/2, the
    // one mirrorCenterOffset seats). Naming it keeps every "the mirror made
    // this" assertion below readable as exactly that.
    const auto mirrorX = [&](int x) { return sceneSize.x - 1 - x; };

    // Clipping the legs, the body's length, or its tiers authors a different
    // animal and saves it anyway, so refuse the scene instead. lx and y0 are
    // derived from sceneSize, so each bound is stated once and the message
    // reports what the guard actually enforces.
    const int minHalfWidth = kAntLegReach + 1;
    if (lx < minHalfWidth || y0 < 1 || gz < kAntBodyTiers) {
        builder.recordError(
            "ant needs a scene at least " + std::to_string((minHalfWidth + 1) * 2) + " x " +
            std::to_string(kAntBodyRows + 2) + " x " + std::to_string(kAntBodyTiers + 1) +
            "; got " + std::to_string(sceneSize.x) + " x " + std::to_string(sceneSize.y) + " x " +
            std::to_string(sceneSize.z)
        );
        return builder.finish();
    }

    // Where each leg's outermost click lands; the assertions below read it back
    // on both sides of the mirror.
    const int legTipX = lx - kAntLegReach;

    const int abdomenLoY = y0;
    const int abdomenHiY = y0 + 4;
    const int petioleY = y0 + 5;
    const int thoraxLoY = y0 + 6;
    const int thoraxHiY = y0 + 10;
    const int neckY = y0 + 11;
    const int headLoY = y0 + 12;
    const int headHiY = y0 + 13;
    const int antennaLoY = y0 + 14;
    const int antennaHiY = y0 + 15;

    // --- Carve the silhouette out of the seeded ground slab -----------------
    // With the mirror on, each drag clears its own low-x strip and the
    // reflection clears the matching high-x one, so the whole plane is framed
    // from half the gestures. Runs first, while the plane is flat and every
    // corner face is exposed (once a tier sits above it, the (1,1,1) march to
    // the cells behind is occluded). What survives IS the ant's underside: the
    // body outline, plus the two antenna stalks.
    builder.segment("clear_ground");
    builder.enableSymmetry(true, false, false);
    builder.toggleEraseMode();
    builder.dragBox(ivec3(0, 0, gz), ivec3(lx, abdomenLoY - 1, gz));
    builder.dragBox(ivec3(0, antennaHiY + 1, gz), ivec3(lx, sceneSize.y - 1, gz));
    builder.dragBox(ivec3(0, abdomenLoY, gz), ivec3(lx - 3, abdomenHiY, gz));
    builder.dragBox(ivec3(0, petioleY, gz), ivec3(lx - 1, petioleY, gz));
    builder.dragBox(ivec3(0, thoraxLoY, gz), ivec3(lx - 2, thoraxHiY, gz));
    builder.dragBox(ivec3(0, neckY, gz), ivec3(lx - 1, neckY, gz));
    builder.dragBox(ivec3(0, headLoY, gz), ivec3(lx - 2, antennaHiY, gz));
    // The antennae are the head's two outer columns run forward; erase the
    // inner pair the head strip would otherwise have kept.
    builder.dragBox(ivec3(lx, antennaLoY, gz), ivec3(lx, antennaHiY, gz));
    builder.toggleEraseMode();

    // The kept width of each body segment is set by where its erase strip
    // stopped on ONE side — the other side is the mirror's work, so a broken
    // mirror leaves the high-x half of the plane fully slabbed and fails here.
    builder.expectOccupancy(ivec3(lx, abdomenLoY + 2, gz), true, "abdomen_footprint_kept");
    builder
        .expectOccupancy(ivec3(mirrorX(lx - 2), abdomenLoY + 2, gz), true, "abdomen_mirror_kept");
    builder.expectOccupancy(ivec3(lx - 3, abdomenLoY + 2, gz), false, "abdomen_side_cleared");
    builder.expectOccupancy(
        ivec3(mirrorX(lx - 3), abdomenLoY + 2, gz),
        false,
        "abdomen_side_mirror_cleared"
    );
    builder.expectOccupancy(ivec3(lx, petioleY, gz), true, "petiole_kept");
    builder.expectOccupancy(ivec3(lx - 1, petioleY, gz), false, "waist_pinched");
    builder.expectOccupancy(ivec3(mirrorX(lx - 1), petioleY, gz), false, "waist_mirror_pinched");
    builder.expectOccupancy(ivec3(lx - 1, antennaLoY, gz), true, "antenna_kept");
    builder.expectOccupancy(ivec3(mirrorX(lx - 1), antennaHiY, gz), true, "antenna_mirror_kept");
    builder.expectOccupancy(ivec3(lx, antennaLoY, gz), false, "antenna_gap_cleared");
    // F-2d-1 measured the plane fully reachable at 16³ but flagged that a
    // larger scene could push its edges under the left GUI panels. The ant is
    // the first 20³ session, so all four corners are re-instrumented: a
    // swallowed erase drag fails here instead of shipping a slab in the asset.
    builder.expectOccupancy(ivec3(0, 0, gz), false, "ground_corner_lo_cleared");
    builder.expectOccupancy(ivec3(sceneSize.x - 1, 0, gz), false, "ground_corner_hi_x_cleared");
    builder.expectOccupancy(ivec3(0, sceneSize.y - 1, gz), false, "ground_corner_hi_y_cleared");
    builder.expectOccupancy(
        ivec3(sceneSize.x - 1, sceneSize.y - 1, gz),
        false,
        "ground_corner_hi_cleared"
    );

    // --- Abdomen: two domed tiers on their own layer ------------------------
    builder.addLayer();
    builder.segment("abdomen");
    builder.dragBox(ivec3(lx - 1, abdomenLoY + 1, gz - 1), ivec3(lx, abdomenHiY - 1, gz - 1));
    builder.dragBox(ivec3(lx - 1, abdomenLoY + 1, gz - 2), ivec3(lx, abdomenHiY - 2, gz - 2));
    builder
        .expectOccupancy(ivec3(mirrorX(lx), abdomenLoY + 1, gz - 1), true, "abdomen_tier2_mirror");
    builder.expectOccupancy(
        ivec3(mirrorX(lx - 1), abdomenHiY - 1, gz - 1),
        true,
        "abdomen_tier2_mirror_edge"
    );
    builder
        .expectOccupancy(ivec3(mirrorX(lx), abdomenLoY + 1, gz - 2), true, "abdomen_tier3_mirror");

    // --- Thorax: the leg-bearing tier, on its own layer ---------------------
    builder.addLayer();
    builder.segment("thorax");
    builder.dragBox(ivec3(lx - 1, thoraxLoY, gz - 1), ivec3(lx, thoraxHiY, gz - 1));
    builder
        .expectOccupancy(ivec3(mirrorX(lx - 1), thoraxLoY, gz - 1), true, "thorax_mirror_filled");
    builder.expectOccupancy(ivec3(mirrorX(lx), thoraxHiY, gz - 1), true, "thorax_mirror_far_row");
    // The waist is what makes this an ant and not a loaf: the petiole and neck
    // rows stay one tier tall, so a tier drag that overran its y range shows up
    // as a filled cell here rather than as a subtly wrong silhouette.
    builder.expectOccupancy(ivec3(lx, petioleY, gz - 1), false, "waist_stays_one_tier");

    // --- Legs: three chains of -x-face clicks, each mirrored into its pair ---
    // The only gesture in the session that grows sideways out of standing
    // geometry rather than up off the plane, so it is armed with a pick
    // assertion first (F-2c-4) — and in its own segment, since the aim names a
    // cell the very next click builds off (F-2d-2).
    builder.addLayer();
    builder.segment("legs_arm");
    builder.hover(ivec3(lx - 2, thoraxLoY, gz - 1));
    builder.expectPick(ivec3(lx - 1, thoraxLoY, gz - 1), "leg_aim_hits_thorax_face");

    builder.segment("legs");
    const int legRows[3] = {thoraxLoY, thoraxLoY + 2, thoraxLoY + 4};
    for (int legIndex = 0; legIndex < 3; ++legIndex) {
        const int legY = legRows[legIndex];
        // Each click anchors on the one before it, so the chain is what walks
        // the leg outward; the mirror builds its pair at the same time.
        for (int x = lx - 2; x >= legTipX; --x)
            builder.click(ivec3(x, legY, gz - 1));
        const std::string tag = std::to_string(legIndex + 1);
        builder.expectOccupancy(ivec3(legTipX, legY, gz - 1), true, "leg_" + tag + "_tip");
        builder.expectOccupancy(
            ivec3(mirrorX(legTipX), legY, gz - 1),
            true,
            "leg_" + tag + "_mirror_tip"
        );
    }
    // The gaps are what make these six legs rather than two slabs: a chain that
    // smeared along y, or a box fill standing in for the clicks, fills them.
    builder.expectOccupancy(ivec3(legTipX, thoraxLoY + 1, gz - 1), false, "leg_gap_1_empty");
    builder.expectOccupancy(
        ivec3(mirrorX(legTipX), thoraxLoY + 3, gz - 1),
        false,
        "leg_gap_2_mirror_empty"
    );

    // --- Head + eyes on the last layer --------------------------------------
    builder.addLayer();
    builder.segment("head");
    builder.dragBox(ivec3(lx - 1, headLoY, gz - 1), ivec3(lx, headHiY, gz - 1));
    builder.click(ivec3(lx - 1, headHiY, gz - 2));
    builder.expectOccupancy(ivec3(mirrorX(lx - 1), headLoY, gz - 1), true, "head_mirror_filled");
    builder.expectOccupancy(ivec3(mirrorX(lx - 1), headHiY, gz - 2), true, "eye_mirror_placed");
    builder.expectOccupancy(ivec3(lx, neckY, gz - 1), false, "neck_stays_one_tier");

    // --- Layer select + visibility ------------------------------------------
    // Five layers exist now (default, abdomen, thorax, legs, head) and the head
    // is active, so one `[` lands on the legs. Hiding it zeroes only the legs'
    // alpha — the thorax and head cells beside them stay lit, which is what
    // separates "the hide worked" from "`[` selected the wrong layer".
    builder.segment("legs_hide");
    builder.selectPrevLayer();
    builder.toggleActiveLayerVisibility();
    builder.expectOccupancy(ivec3(legTipX, thoraxLoY, gz - 1), false, "legs_hidden_read_empty");
    builder.expectOccupancy(ivec3(mirrorX(legTipX), thoraxLoY, gz - 1), false, "leg_mirror_hidden");
    builder.expectOccupancy(ivec3(lx - 1, thoraxLoY, gz - 1), true, "thorax_lit_while_legs_hidden");
    builder.expectOccupancy(
        ivec3(mirrorX(lx - 1), headLoY, gz - 1),
        true,
        "head_lit_while_legs_hidden"
    );

    // Restore before saving — a hidden layer's voxels write out as empty.
    builder.segment("legs_show");
    builder.toggleActiveLayerVisibility();
    builder.selectNextLayer();
    builder.expectOccupancy(ivec3(legTipX, thoraxLoY, gz - 1), true, "legs_shown_again");

    // --- Save + reload round-trip -------------------------------------------
    builder.segment("save");
    builder.save();
    builder.expectOccupancy(ivec3(lx, thoraxLoY, gz - 1), true, "scene_intact_after_save");

    // Five layers through the round-trip (the mushroom took two), so the
    // reload checks read one cell per authored layer plus the waist, which
    // must still be empty — a load that filled it would read as "intact".
    builder.segment("reload");
    builder.reload();
    builder.expectOccupancy(
        ivec3(mirrorX(lx), abdomenLoY + 1, gz - 2),
        true,
        "abdomen_survives_reload"
    );
    builder
        .expectOccupancy(ivec3(mirrorX(lx - 1), thoraxLoY, gz - 1), true, "thorax_survives_reload");
    builder.expectOccupancy(
        ivec3(mirrorX(legTipX), thoraxLoY + 4, gz - 1),
        true,
        "leg_survives_reload"
    );
    builder
        .expectOccupancy(ivec3(mirrorX(lx - 1), antennaHiY, gz), true, "antenna_survives_reload");
    builder.expectOccupancy(ivec3(lx, petioleY, gz - 1), false, "waist_survives_reload");

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
    case Id::ANT:
        return detail::buildAnt(sceneSize, sceneOrigin);
    case Id::NONE:
        break;
    }
    return Recipe{};
}

} // namespace IRVoxelEditor::Session

#endif /* IR_VOXEL_EDITOR_SESSIONS_H */
