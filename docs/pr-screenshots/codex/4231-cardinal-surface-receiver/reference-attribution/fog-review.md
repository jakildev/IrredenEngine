# Fog control visual review — #4231

Reviewed all 32 RGB-changed pairs found among 62 matched captures. Both before and after trees contain 62 PNG captures at review time (the assignment described 65); no unmatched PNG paths were found. Every changed pair was inspected as a before/after contact sheet cropped to the RGB change bounds plus 12 pixels, using nearest-neighbor resizing. No production edits or engine runs were made.

## Findings

No new visual blocker identified in these matched fog controls. Changes are localized to contact shadows and projected-shadow boundaries. This is a visual assessment, not an independent ray/geometry proof of every changed pixel.

- `edge`, `edge_smooth`, `edge_zcost*`, `ceiling*`, and `entity_reveal*`: dark skirts around the bases of the green, blue, orange and other pillars largely disappear. Their colored face boundaries stay aligned. The broad fog circles, smooth falloff bands and reveal transitions retain their positions and character.
- `detached*`: floor shadow edges become sharply stepped in screen pixels, while the old blurred/contact skirt disappears. The large detached object's visible faces remain positioned consistently. The 90-degree view keeps the appropriate opposite shadow direction; no shape clipping or displaced face edge is apparent.
- `edge_sdf_blocker`: the floating purple cube remains unchanged visually; its floor shadow becomes a more sharply bounded polygon. No new triangular scatter or gaps are visible.
- `occlusion`, `occlusion_blocker`, `occlusion_blocker_inert`, and `occlusion_two_sources`: the long wall retains its shape and fog treatment. The formerly broad dark fringe narrows, with residual shadow aligned to the wall base. No new breaks along that edge are visible.
- `occlusion_column_top`: the bright magenta diagnostic region remains present in both arms; it is not newly introduced here. Shadow changes occur around the brown/blue object bases.

## Limitations

Some changed pixels cross exact black/nonblack, especially where shadows meet fog darkness. This is not sufficient evidence of silhouette movement: exact black also encodes fully shadowed/fogged surfaces. Counts below call these black-mask changes, not geometry changes. Geometry/depth IDs would be required to prove silhouette identity independently. Changes to cast-shadow footprint are expected under the new sampling route, but these beauty images alone do not prove their exact geometric correctness. No claim about all fog states or motion between these fixed captures.

## Inventory

Columns: relative capture, RGB changed pixels, change bounds (left, top, right, bottom), black-mask changed pixels.

- `ceiling/fog_ceiling9.png`: 12472; `(896, 712, 1660, 910)`; 0.
- `ceiling_treatment/fog_ceiling_treatment9.png`: 12472; `(896, 712, 1660, 910)`; 0.
- `detached/fog_detached_edge_yaw90_zoom5.png`: 2888; `(1336, 572, 1696, 754)`; 56.
- `detached/fog_detached_edge_zoom5.png`: 2824; `(1224, 814, 1588, 936)`; 64.
- `detached/fog_detached_edge_zoom9.png`: 10336; `(1180, 906, 1900, 1154)`; 400.
- `detached_body/fog_detached_body.png`: 3784; `(1180, 924, 1644, 1154)`; 88.
- `detached_body_soft/fog_detached_body_soft.png`: 4592; `(1152, 908, 1544, 1104)`; 96.
- `detached_exempt/fog_detached_exempt.png`: 3592; `(1208, 936, 1644, 1154)`; 88.
- `detached_field_hidden/fog_detached_field_hidden.png`: 10144; `(1208, 906, 1900, 1154)`; 400.
- `edge/fog_edge_zoom14.png`: 61736; `(428, 668, 2388, 1440)`; 144.
- `edge/fog_edge_zoom5.png`: 3760; `(1068, 706, 1488, 930)`; 48.
- `edge/fog_edge_zoom9.png`: 15376; `(860, 694, 1812, 1140)`; 288.
- `edge_sdf_blocker/fog_edge_sdf_blocker_zoom5.png`: 4472; `(1068, 706, 1488, 930)`; 48.
- `edge_sdf_blocker/fog_edge_sdf_blocker_zoom9.png`: 19080; `(860, 694, 1812, 1140)`; 288.
- `edge_smooth/fog_edge_smooth14.png`: 59968; `(436, 668, 2120, 1432)`; 296.
- `edge_smooth/fog_edge_smooth5.png`: 3176; `(1068, 706, 1488, 896)`; 16.
- `edge_smooth/fog_edge_smooth9.png`: 12840; `(864, 694, 1692, 1076)`; 64.
- `edge_zcost/fog_edge_zcost5.png`: 2416; `(1096, 732, 1348, 818)`; 0.
- `edge_zcost/fog_edge_zcost9.png`: 8048; `(916, 742, 1404, 910)`; 0.
- `edge_zcost_asym/fog_edge_zcost_asym5.png`: 1040; `(1304, 826, 1440, 864)`; 0.
- `edge_zcost_asym/fog_edge_zcost_asym9.png`: 3768; `(1336, 932, 1596, 1008)`; 0.
- `edge_zcost_ceiling/fog_edge_zcost_ceiling5.png`: 2416; `(1096, 732, 1348, 818)`; 0.
- `edge_zcost_ceiling/fog_edge_zcost_ceiling9.png`: 8048; `(916, 742, 1404, 910)`; 0.
- `edge_zcost_ceiling_paint/fog_edge_zcost_ceiling_paint5.png`: 2416; `(1096, 732, 1348, 818)`; 0.
- `edge_zcost_ceiling_paint/fog_edge_zcost_ceiling_paint9.png`: 8048; `(916, 742, 1404, 910)`; 0.
- `entity_reveal/fog_entity_reveal.png`: 32112; `(72, 360, 2452, 1242)`; 480.
- `entity_reveal_soft/fog_entity_reveal_soft.png`: 27112; `(208, 360, 2344, 946)`; 160.
- `occlusion/fog_occlusion_ground.png`: 8464; `(1028, 720, 1568, 992)`; 0.
- `occlusion_blocker/fog_occlusion_blocker.png`: 4936; `(1044, 688, 1548, 950)`; 0.
- `occlusion_blocker_inert/fog_occlusion_blocker_inert.png`: 9408; `(1008, 688, 1548, 950)`; 0.
- `occlusion_column_top/fog_occlusion_column_top.png`: 9368; `(1080, 854, 1828, 1088)`; 0.
- `occlusion_two_sources/fog_occlusion_two_sources.png`: 2456; `(1036, 710, 1452, 912)`; 24.

Unmatched before-only: none.
Unmatched after-only: none.

The retained native pairs are linked in [the attribution table](README.md).
