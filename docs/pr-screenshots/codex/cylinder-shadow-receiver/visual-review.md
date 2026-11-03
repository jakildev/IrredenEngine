# Cylinder receiver native image review

Reviewed original engine captures using view_image (no image edits), from `/tmp/curved-receiver-matrix/{before,after}`.

## Scope and measurements

- External cylinder before/after: visually inspected all eight yaw angles (0 through315, step45).
- Self-only cylinder before/after: inspected45,135,225,315.
- Shadows-disabled cylinder: inspected45,135,315; exact comparison covers all8.
- Pitched cylinder external before/after: inspected45,135,315; pitched shadows-disabled inspected45; exact comparison covers all8.
- `comparison.json` (primary cylinder rows):24pairedframes,15changed,125464changedpixels, zero expectation violations. External changes8/8, self7/8, shadows-disabled0/8. Identical settled cameras/commands and preserved image/source hashes verified.
- `comparison.json` (pitched cylinder rows):16pairedframes,8changed,71296changedpixels. External changes8/8, shadows-disabled0/8. No expectation violations.
- `material-attribution.json`:for all16 self/external primarypairs, every changed pixel remains inside blue-dominant receiver material (B>R and B>G), with zero blue-material coverage differences. The orange caster, floor, and floor shadow are untouched in these captures. This is a fixture-specific attribution check, not an engine semantic-ID oracle.

## Visible improvements

External projections now have a single hard edge in every inspected quadrant. Former fuzzy or doubled stair fringes no longer spread outward from the local projection. Examples:45/315 on the cylinder side,90/270 where projection crosses the top/side rim,180/225 on the top cap. At yaw0 the shadow footprint extends farther onto the side after correction; this is a changed receiver location, not a floor-footprint change.

The large self-shadow transition at135 becomes a coherent straight vertical boundary, rather than a noisy feathered band. Pitched135 similarly produces a straight slanted self-shadow boundary. Pitched45/315 external projection keeps a crisp boundary conforming to the rotated surface. I see no newly detached shadow islands, cap/side coverage gaps, or unrelated geometry changes in these inspected frames.

These pictures support the implementation direction, but smooth appearance is not proof of geometric correctness. That proof remains the independent field/ray tests and routing exclusions. Do not describe this as every curved display artifact being fixed.

## Explicit unresolved artifacts

1. A conspicuous dotted vertical band remains near the side's display-normal transition, particularly at45/90/315. It is present with shadows disabled. The pitched equivalent is a repeated curved/diagonal comb across the side (very visible pitched45 and315). This extension preserves it. Its cause is upstream display/face-slot selection or reconstruction/direct face lighting, not this sun-visibility calculation. The no-shadow control isolates sun shadows; it does not by itself distinguish valid finite exposed faces from incorrect parity/ownership. Do not smooth normals or suppress it without proving the representation contract.
2. Small stepped rim pixels remain. The output still uses finite canvas texels/display trixels; this patch does not reconstruct a continuous per-framebuffer surface or add antialiasing. There is no basis to claim all remaining rim steps are erroneous.
3. Cone and torus receivers intentionally retain their old behavior. Their nearer empty-integer-sample intervals require retained connected-component selection; visually good first-root captures would not make that method correct.
4. The8yaw matrix is discrete coverage. It does not prove absence of temporal transitions between sampled angles.

## Recommended next diagnostic for the display band

Pick a shadows-disabled cylinder side row through the band and record elected owner key, face slot, raw depth and parity for neighboring canvas texels. Recover each producer sample's finite emitted display-face triangle and compare against an independent geometric visibility oracle on that retained-sample representation. Use a material/normal-ID diagnostic so Lambert colors do not obscure ownership. Only then decide whether the comb is valid exposed stair geometry or overlapping SDF sample faces/parity. Keep continuous SDF geometry and retained finite-face geometry as separate explicit reference contracts.

The bounded cylinder receiver change is visually consistent with its tests and improves the intended shadow defect. The side display band remains an explicit follow-up, not a reason to alter correct shadow ray orientation or to claim visual completion.
