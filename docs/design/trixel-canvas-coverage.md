# Logical viewport and trixel backing coverage

The main canvas has two extents. Its `C_SizeTriangles` is the logical viewport
used by camera projection, conservative culling, fog windows and per-axis face
storage. `C_TriangleCanvasTextures::size_` is the physical texture backing used
by producers, texture reads, depth resolve and texel-to-world recovery.

For logical extent L, effective subdivision density D and zoom Z, each axis
requires at least `L * max(1, D / Z)` backing texels. The gather multiplies its
model extent by `backing / logical`, keeping each texel at the same screen size.
Changing only gather scale loses clipped geometry or stretches object size.
Growth is rounded to a multiple of four so the centered integer origin retains
its triangle parity. The camera pixel-offset decomposition is unchanged.

`CanvasCoverage::syncMainBacking` runs at the producer frame boundary before
resource pointers are acquired. It retains high-water capacity and resizes the
color/depth/entity-id triple, Hi-Z and optional AO/shadow targets together.
Canvas-owned descriptor and source-face resources survive; descriptor samples
are invalidated. Per-axis face storage and compaction/sort scratch retain their
logical extent; only the cardinal resolve target follows backing growth, even
when the face store was parked. New depth and Hi-Z textures receive the empty
sentinel. Zoom/density transitions invalidate lagged occlusion use.

Private canvases retain their explicitly allocated extents and existing placement
contracts. World fog and light-volume resources are not replaced by backing growth.

## Cost and validation boundaries

High density costs storage as well as face samples. A 642×722 logical canvas at
zoom 4 and effective density 16 needs 2570×2890 backing texels, about sixteen times
the cardinal texture area. Capacity is retained after zoom/density changes; this
policy avoids repeated allocation but is not a sparse screen-budget solution.
The supported density ceiling is unchanged. Very large viewport/density choices
still require device memory and texture-limit budgeting before qualification.

Coverage math tests span density 1–16 and supported zooms. Native resource tests
check empty depth/Hi-Z sentinels, retained descriptor/source/face/scratch resources,
parked resume, no-op resize and destruction. Screenshot and throughput evidence
belongs with each concrete control; neither these tests nor a clipped historical
profile establishes million-entity throughput.
