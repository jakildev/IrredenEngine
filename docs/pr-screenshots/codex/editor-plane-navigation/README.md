Native Metal captures from IRVoxelEditor at the default window size.

- [Before](swipe-before.png) and [after](swipe-after.png): first horizontal swipe in the `viewport_scroll` session, starting at 30°. The gold marker moves from the pointer's hit point to the center of the construction plane.
- [Keyboard shortcuts](mac-shortcuts.png): the F1 overlay in the document editor on macOS. Cmd+0 resets the view; Option-drag orbits around the point under the cursor.

To reproduce the navigation checks, run `python3 scripts/gui-verify.py --no-build IRVoxelEditor -- --gui-session viewport_scroll --gui-layout-check`. Run again with `--home-zoom 0` to check preserving the zoom on reset.
