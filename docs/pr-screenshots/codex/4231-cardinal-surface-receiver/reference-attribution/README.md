# Native reference attribution

Each pair uses the same executable and staged assets except the sun-shadow compute body: before uses the pre-4231 body; after restores this implementation. Both arms exit cleanly. These are controlled shader counterfactuals, not full parent rebuilds. Native frames are unmodified.

The older full-suite run had 59 reference failures across 172 checks. References are refreshed after inspecting the changed regions and retaining these pairs. Thresholds and shot inventories are unchanged. Rows marked inherited drift already differed with the prior receiver; the latest gather mapping and earlier stack receiver repairs also changed these references. Those differences are not attributed to #4231.

| Demo / shot | Receiver changed | Inherited reference drift | Before | After |
|---|---|---|---|---|
| canvas_stress/compare_yaw0 | False | True | [before](canvas_stress/compare_yaw0/before.png) | [after](../../../../../creations/demos/canvas_stress/test/references/macos-debug/compare_yaw0.png) |
| canvas_stress/compare_yaw_q | False | True | [before](canvas_stress/compare_yaw_q/before.png) | [after](../../../../../creations/demos/canvas_stress/test/references/macos-debug/compare_yaw_q.png) |
| canvas_stress/revoxelize_solids | False | True | [before](canvas_stress/revoxelize_solids/before.png) | [after](../../../../../creations/demos/canvas_stress/test/references/macos-debug/revoxelize_solids.png) |
| canvas_stress/so3_grid_cross | False | True | [before](canvas_stress/so3_grid_cross/before.png) | [after](../../../../../creations/demos/canvas_stress/test/references/macos-debug/so3_grid_cross.png) |
| canvas_stress/so3_offsnap_disc | False | True | [before](canvas_stress/so3_offsnap_disc/before.png) | [after](../../../../../creations/demos/canvas_stress/test/references/macos-debug/so3_offsnap_disc.png) |
| canvas_stress/so3_offsnap_wide | False | True | [before](canvas_stress/so3_offsnap_wide/before.png) | [after](../../../../../creations/demos/canvas_stress/test/references/macos-debug/so3_offsnap_wide.png) |
| canvas_stress/so3_smooth_sweep | False | True | [before](canvas_stress/so3_smooth_sweep/before.png) | [after](../../../../../creations/demos/canvas_stress/test/references/macos-debug/so3_smooth_sweep.png) |
| canvas_stress/so3_wide_parity | False | True | [before](canvas_stress/so3_wide_parity/before.png) | [after](../../../../../creations/demos/canvas_stress/test/references/macos-debug/so3_wide_parity.png) |
| canvas_stress/detach_grid | True | False | [before](canvas_stress/detach_grid/before.png) | [after](../../../../../creations/demos/canvas_stress/test/references/macos-debug/detach_grid.png) |
| canvas_stress/impulse_midspin | True | False | [before](canvas_stress/impulse_midspin/before.png) | [after](../../../../../creations/demos/canvas_stress/test/references/macos-debug/impulse_midspin.png) |
| canvas_stress/impulse_settled | True | False | [before](canvas_stress/impulse_settled/before.png) | [after](../../../../../creations/demos/canvas_stress/test/references/macos-debug/impulse_settled.png) |
| canvas_stress/modeswitch_grid | True | False | [before](canvas_stress/modeswitch_grid/before.png) | [after](../../../../../creations/demos/canvas_stress/test/references/macos-debug/modeswitch_grid.png) |
| canvas_stress/modeswitch_grid_return | True | False | [before](canvas_stress/modeswitch_grid_return/before.png) | [after](../../../../../creations/demos/canvas_stress/test/references/macos-debug/modeswitch_grid_return.png) |
| shape_debug/lod_swap_zoom1_cube | True | False | [before](shape_debug/lod_swap_zoom1_cube/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/lod_swap_zoom1_cube.png) |
| shape_debug/lod_swap_zoom2_cube | True | False | [before](shape_debug/lod_swap_zoom2_cube/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/lod_swap_zoom2_cube.png) |
| shape_debug/lod_swap_zoom4_cone | True | False | [before](shape_debug/lod_swap_zoom4_cone/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/lod_swap_zoom4_cone.png) |
| shape_debug/lod_swap_zoom8_cone | True | False | [before](shape_debug/lod_swap_zoom8_cone/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/lod_swap_zoom8_cone.png) |
| shape_debug/zoom16_lod_fine_sphere | True | False | [before](shape_debug/zoom16_lod_fine_sphere/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/zoom16_lod_fine_sphere.png) |
| shape_debug/zoom1_odd_offset | True | False | [before](shape_debug/zoom1_odd_offset/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/zoom1_odd_offset.png) |
| shape_debug/zoom1_origin | True | False | [before](shape_debug/zoom1_origin/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/zoom1_origin.png) |
| shape_debug/zoom2_origin | True | False | [before](shape_debug/zoom2_origin/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/zoom2_origin.png) |
| shape_debug/zoom4_offset_3_5 | True | False | [before](shape_debug/zoom4_offset_3_5/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/zoom4_offset_3_5.png) |
| shape_debug/zoom4_origin | True | False | [before](shape_debug/zoom4_origin/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/zoom4_origin.png) |
| shape_debug/zoom4_pan16_yaw0_pivot | True | True | [before](shape_debug/zoom4_pan16_yaw0_pivot/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/zoom4_pan16_yaw0_pivot.png) |
| shape_debug/zoom4_pan16_yaw180_pivot | True | False | [before](shape_debug/zoom4_pan16_yaw180_pivot/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/zoom4_pan16_yaw180_pivot.png) |
| shape_debug/zoom4_pan16_yaw90_pivot | True | False | [before](shape_debug/zoom4_pan16_yaw90_pivot/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/zoom4_pan16_yaw90_pivot.png) |
| shape_debug/zoom4_yaw180 | True | False | [before](shape_debug/zoom4_yaw180/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/zoom4_yaw180.png) |
| shape_debug/zoom4_yaw270 | True | False | [before](shape_debug/zoom4_yaw270/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/zoom4_yaw270.png) |
| shape_debug/zoom4_yaw90 | True | False | [before](shape_debug/zoom4_yaw90/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/zoom4_yaw90.png) |
| shape_debug/zoom8_origin | True | False | [before](shape_debug/zoom8_origin/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/zoom8_origin.png) |
| shape_debug/zoom8_yaw180 | True | False | [before](shape_debug/zoom8_yaw180/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/zoom8_yaw180.png) |
| shape_debug/lod_dense_swap_z8 | True | False | [before](shape_debug/lod_dense_swap_z8/before.png) | [after](../../../../../creations/demos/shape_debug/test/references/macos-debug/lod_dense_swap_z8.png) |
| lighting/zoom1_origin | True | False | [before](lighting/zoom1_origin/before.png) | [after](../../../../../creations/demos/lighting/test/references/macos-debug/zoom1_origin.png) |
| lighting/zoom2_origin | True | True | [before](lighting/zoom2_origin/before.png) | [after](../../../../../creations/demos/lighting/test/references/macos-debug/zoom2_origin.png) |
| lighting/zoom4_offset_3_5 | True | True | [before](lighting/zoom4_offset_3_5/before.png) | [after](../../../../../creations/demos/lighting/test/references/macos-debug/zoom4_offset_3_5.png) |
| lighting/zoom4_origin | True | True | [before](lighting/zoom4_origin/before.png) | [after](../../../../../creations/demos/lighting/test/references/macos-debug/zoom4_origin.png) |
| lighting/zoom8_origin | True | True | [before](lighting/zoom8_origin/before.png) | [after](../../../../../creations/demos/lighting/test/references/macos-debug/zoom8_origin.png) |
| fog_demo/fog_ceiling9 | True | False | [before](fog_demo/fog_ceiling9/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_ceiling9.png) |
| fog_demo/fog_ceiling_treatment9 | True | False | [before](fog_demo/fog_ceiling_treatment9/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_ceiling_treatment9.png) |
| fog_demo/fog_zoom2_origin | False | True | [before](fog_demo/fog_zoom2_origin/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_zoom2_origin.png) |
| fog_demo/fog_zoom4_origin | False | True | [before](fog_demo/fog_zoom4_origin/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_zoom4_origin.png) |
| fog_demo/fog_zoom8_origin | False | True | [before](fog_demo/fog_zoom8_origin/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_zoom8_origin.png) |
| fog_demo/fog_detached_edge_yaw90_zoom5 | True | False | [before](fog_demo/fog_detached_edge_yaw90_zoom5/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_detached_edge_yaw90_zoom5.png) |
| fog_demo/fog_detached_edge_zoom5 | True | False | [before](fog_demo/fog_detached_edge_zoom5/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_detached_edge_zoom5.png) |
| fog_demo/fog_detached_edge_zoom9 | True | False | [before](fog_demo/fog_detached_edge_zoom9/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_detached_edge_zoom9.png) |
| fog_demo/fog_detached_body | True | False | [before](fog_demo/fog_detached_body/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_detached_body.png) |
| fog_demo/fog_detached_body_soft | True | False | [before](fog_demo/fog_detached_body_soft/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_detached_body_soft.png) |
| fog_demo/fog_detached_exempt | True | False | [before](fog_demo/fog_detached_exempt/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_detached_exempt.png) |
| fog_demo/fog_detached_field_hidden | True | False | [before](fog_demo/fog_detached_field_hidden/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_detached_field_hidden.png) |
| fog_demo/fog_edge_zoom14 | True | False | [before](fog_demo/fog_edge_zoom14/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_edge_zoom14.png) |
| fog_demo/fog_edge_zoom5 | True | False | [before](fog_demo/fog_edge_zoom5/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_edge_zoom5.png) |
| fog_demo/fog_edge_zoom9 | True | False | [before](fog_demo/fog_edge_zoom9/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_edge_zoom9.png) |
| fog_demo/fog_edge_sdf_blocker_zoom5 | True | False | [before](fog_demo/fog_edge_sdf_blocker_zoom5/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_edge_sdf_blocker_zoom5.png) |
| fog_demo/fog_edge_sdf_blocker_zoom9 | True | False | [before](fog_demo/fog_edge_sdf_blocker_zoom9/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_edge_sdf_blocker_zoom9.png) |
| fog_demo/fog_edge_smooth14 | True | False | [before](fog_demo/fog_edge_smooth14/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_edge_smooth14.png) |
| fog_demo/fog_edge_smooth5 | True | False | [before](fog_demo/fog_edge_smooth5/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_edge_smooth5.png) |
| fog_demo/fog_edge_smooth9 | True | False | [before](fog_demo/fog_edge_smooth9/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_edge_smooth9.png) |
| fog_demo/fog_edge_zcost5 | True | False | [before](fog_demo/fog_edge_zcost5/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_edge_zcost5.png) |
| fog_demo/fog_edge_zcost9 | True | False | [before](fog_demo/fog_edge_zcost9/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_edge_zcost9.png) |
| fog_demo/fog_edge_zcost_asym5 | True | False | [before](fog_demo/fog_edge_zcost_asym5/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_edge_zcost_asym5.png) |
| fog_demo/fog_edge_zcost_asym9 | True | False | [before](fog_demo/fog_edge_zcost_asym9/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_edge_zcost_asym9.png) |
| fog_demo/fog_edge_zcost_ceiling5 | True | False | [before](fog_demo/fog_edge_zcost_ceiling5/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_edge_zcost_ceiling5.png) |
| fog_demo/fog_edge_zcost_ceiling9 | True | False | [before](fog_demo/fog_edge_zcost_ceiling9/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_edge_zcost_ceiling9.png) |
| fog_demo/fog_edge_zcost_ceiling_paint5 | True | False | [before](fog_demo/fog_edge_zcost_ceiling_paint5/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_edge_zcost_ceiling_paint5.png) |
| fog_demo/fog_edge_zcost_ceiling_paint9 | True | False | [before](fog_demo/fog_edge_zcost_ceiling_paint9/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_edge_zcost_ceiling_paint9.png) |
| fog_demo/fog_entity_reveal | True | False | [before](fog_demo/fog_entity_reveal/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_entity_reveal.png) |
| fog_demo/fog_entity_reveal_soft | True | False | [before](fog_demo/fog_entity_reveal_soft/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_entity_reveal_soft.png) |
| fog_demo/fog_occlusion_ground | True | False | [before](fog_demo/fog_occlusion_ground/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_occlusion_ground.png) |
| fog_demo/fog_occlusion_blocker | True | False | [before](fog_demo/fog_occlusion_blocker/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_occlusion_blocker.png) |
| fog_demo/fog_occlusion_blocker_inert | True | True | [before](fog_demo/fog_occlusion_blocker_inert/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_occlusion_blocker_inert.png) |
| fog_demo/fog_occlusion_column_top | True | False | [before](fog_demo/fog_occlusion_column_top/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_occlusion_column_top.png) |
| fog_demo/fog_occlusion_two_sources | True | False | [before](fog_demo/fog_occlusion_two_sources/before.png) | [after](../../../../../creations/demos/fog_demo/test/references/macos-debug/fog_occlusion_two_sources.png) |

Capture commands, shader SHA-256 identities and clean-exit lines are retained under
`captures/`, linked by `source_manifests` in the summary manifest. The reproduced
[capture driver](capture-driver.py.txt) shows the bounded asset replacement and
restoration. Executable hashes were not recorded at capture time, so the claim of
one executable rests on this controlled procedure, not a retained binary digest.
The rebuilt whole-suite run is separate evidence for the integrated source head.

The comparison fixture uses `capture_offset: -3`; its two selected frames were
recaptured with the [tail-offset driver](compare-capture-driver.py.txt). An initial
diagnostic selected the default prefix incorrectly; those two images were
replaced before publication of this attribution. Both corrected before/after
pairs are RGB-identical, so their reference drift is inherited.
