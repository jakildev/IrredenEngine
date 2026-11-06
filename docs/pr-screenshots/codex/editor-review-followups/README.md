Native macOS Metal captures from the editor's default window.

- outline.png and filled.png show the material_tools workflow before and after Bucket.
- picker.png shows a sampled RGB absent from the palette selected as the foreground color, with Recolor active.
- shapes.png shows the Shapes controls and the generated 7×7×7 cone.

Reproduce with `python3 scripts/gui-verify.py --no-build IRVoxelEditor -- --gui-session material_tools --gui-layout-check` and the same invocation with `shapes_workflow`.
